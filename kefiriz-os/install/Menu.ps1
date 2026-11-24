# Latin-1 is a byte-preserving mapping for INIs, including unknown comment encodings.
$MenuEncoding = [Text.Encoding]::GetEncoding(28591)

function MenuText([string]$file) {
    if (![IO.File]::Exists($file)) { return '' }
    if (([IO.FileInfo]$file).Length -gt 262144) { throw "Unexpectedly large boot menu: $file" }
    $text = $MenuEncoding.GetString([IO.File]::ReadAllBytes($file))
    if ($text.Contains([char]0)) { throw "Unsupported boot menu encoding: $file" }
    return $text
}
function PutMenu([string]$file,[string]$value) {
    [IO.File]::WriteAllBytes($file,$MenuEncoding.GetBytes($value))
}
function Section([string]$text,[string]$name,[switch]$Optional) {
    $headers = [regex]::Matches($text,'(?m)^(?:\xEF\xBB\xBF)?\[([^\]\r\n]+)\][ \t]*(?=\r?$)')
    $hit = $null
    for ($i=0;$i -lt $headers.Count;$i++) {
        if ($headers[$i].Groups[1].Value -cne $name) { continue }
        if ($null -ne $hit) { throw "Duplicate boot menu entry [$name]." }
        $next = if ($i+1 -lt $headers.Count) { $headers[$i+1].Index } else { $text.Length }
        $h = $headers[$i]
        $hit = [pscustomobject]@{Start=$h.Index;End=$next;TitleStart=$h.Groups[1].Index;HeaderEnd=$h.Index+$h.Length;Body=$text.Substring($h.Index,$next-$h.Index)}
    }
    if ($null -eq $hit -and !$Optional) { throw "Missing boot menu entry [$name]." }
    return $hit
}
function KeyMatch([string]$body,[string]$name) {
    $found = [regex]::Matches($body,'(?m)^([ \t]*'+[regex]::Escape($name)+'[ \t]*=[ \t]*)([^\r\n]*?)([ \t]*)(?=\r?$)')
    if ($found.Count -gt 1) { throw "Duplicate boot configuration key: $name" }
    if ($found.Count) { return $found[0] }
}
function Newline([string]$text) { if ($text.Contains("`r`n")) { return "`r`n" }; return "`n" }
function PutConfig([string]$text,[string]$key,[string]$value) {
    $nl = Newline $text
    if (!$text) { return "[config]$nl$key=$value$nl" }
    $part = Section $text 'config'
    $m = KeyMatch $part.Body $key
    if (!$m) { return $text.Insert($part.HeaderEnd,$nl+$key+'='+$value) }
    $group = $m.Groups[2]
    return $text.Remove($part.Start+$group.Index,$group.Length).Insert($part.Start+$group.Index,$value)
}
function NumericConfig([string]$text,[string]$key) {
    if (!$text) { return '0' }
    $m = KeyMatch (Section $text 'config').Body $key
    if (!$m) { return '0' }
    if ($m.Groups[2].Value -cnotmatch '^[0-9]{1,3}$') { throw "Unsupported $key in boot menu." }
    return $m.Groups[2].Value
}
function OsBlock([string]$text,[string]$entry) {
    $nl = Newline $text
    return (($entry.TrimEnd([char]13,[char]10) -split '\r?\n') -join $nl)+$nl
}
function BootMenuEntry([string]$entry) {
    $nl = Newline $entry
    return $entry.TrimEnd([char]13,[char]10) + $nl + 'r2p_action=bootloader' + $nl
}
function ManagedOsBlock([string]$text,[string]$entry) {
    $os = Section $text 'KEFIRIZ OS'
    $desired = OsBlock $os.Body (BootMenuEntry $entry)
    $legacy = OsBlock $os.Body $entry
    foreach ($block in @($desired,$legacy)) {
        if ($os.Body.StartsWith($block,[StringComparison]::Ordinal) -and
            !$os.Body.Substring($block.Length).Trim().Length) {
            return [pscustomobject]@{Start=$os.Start;Block=$block;Desired=$desired}
        }
    }
    throw 'The KEFIRIZ OS boot entry has been edited. Its current configuration was preserved.'
}
function WithBootMenuReturn([string]$text,[string]$entry) {
    $managed = ManagedOsBlock $text $entry
    if ($managed.Block -ceq $managed.Desired) { return $text }
    return $text.Remove($managed.Start,$managed.Block.Length).Insert($managed.Start,$managed.Desired)
}
function PrepareMain([string]$text,[string]$entry,[string]$card) {
    if ((Section $text 'KEFIRIZ OS' -Optional) -or (Section $text 'KEFIRIZ Meridian' -Optional)) { throw 'An independent KEFIRIZ boot menu entry already exists.' }
    $games = Section $text 'KEFIRIZ'
    $payload = KeyMatch $games.Body 'payload'
    $packageEntries = @('fss0','pkg3' | ForEach-Object { $kv=KeyMatch $games.Body $_; if ($kv) { $kv } })
    $stock = KeyMatch $games.Body 'stock'
    if (($stock -and $stock.Groups[2].Value -ceq '1') -or
        !(($payload -and !$packageEntries.Count -and $payload.Groups[2].Value -ceq 'bootloader/payloads/fusee.bin' -and
          [IO.File]::Exists((Child $card 'bootloader/payloads/fusee.bin'))) -or
          ($packageEntries.Count -eq 1 -and !$payload -and $packageEntries[0].Groups[2].Value -ceq 'atmosphere/package3'))) {
        throw 'The existing KEFIRIZ entry has an unrecognized boot route.'
    }
    [void](NumericConfig $text 'autoboot')
    $text = PutConfig $text 'autoboot' '0'
    $games = Section $text 'KEFIRIZ'
    $block = OsBlock $text $entry
    $prefix = if ($games.End -gt 0 -and $text[$games.End-1] -ne [char]10) { Newline $text } else { '' }
    $text = $text.Insert($games.End,$prefix+$block)
    return $text.Remove($games.TitleStart,7).Insert($games.TitleStart,'KEFIRIZ Meridian')
}
function MergeMenuRestore([string]$text,[string]$original,[string]$installed,[string]$entry,[bool]$main) {
    if ($text -ceq $original) { return $original }
    if ($text -ceq $installed) { return $original }
    if (!$text) { throw 'The boot configuration was removed after installation.' }
    if (!$main) {
        if ((NumericConfig $text 'homescreen') -ceq '2') { return PutConfig $text 'homescreen' (NumericConfig $original 'homescreen') }
        return $text
    }
    $os = ManagedOsBlock $text $entry
    $boot = [int](NumericConfig $text 'autoboot')
    if ($boot -and (NumericConfig $text 'autoboot_list') -ceq '0') {
        $before = [regex]::Matches($text.Substring(0,$os.Start),'(?m)^\[(?!config\])[^\]\r\n]+\][ \t]*(?=\r?$)').Count
        if ($boot -eq $before+1) { $boot=0 } elseif ($boot -gt $before+1) { $boot-- }
    } elseif (!$boot) { $boot=[int](NumericConfig $original 'autoboot') }
    $text = $text.Remove($os.Start,$os.Block.Length)
    $games = Section $text 'KEFIRIZ Meridian' -Optional
    if ($games -and !(Section $text 'KEFIRIZ' -Optional)) { $text=$text.Remove($games.TitleStart,'KEFIRIZ Meridian'.Length).Insert($games.TitleStart,'KEFIRIZ') }
    return PutConfig $text 'autoboot' ([string]$boot)
}
