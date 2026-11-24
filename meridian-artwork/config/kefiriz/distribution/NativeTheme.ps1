# Deploy.ps1 holds the card lock and supplies its checked file operations.
$KefirizNativeLayouts = @(
    '0100000000001000/romfs/lyt/Flaunch.szs',
    '0100000000001000/romfs/lyt/Notification.szs',
    '0100000000001000/romfs/lyt/Set.szs',
    '0100000000001000/romfs/lyt/Entrance.szs',
    '0100000000001007/romfs/lyt/Psl.szs',
    '0100000000001013/romfs/lyt/MyPage.szs',
    '0100000000001000/romfs/lyt/ResidentMenu.szs'
) | ForEach-Object { 'atmosphere/contents/' + $_ }
$KefirizNativeFlags = @('0100000000001000','0100000000001007','0100000000001013') |
    ForEach-Object { 'atmosphere/contents/' + $_ + '/fsmitm.flag' }
$KefirizNativePending = 'config/kefiriz/native-themes/pending'
$KefirizPendingSha = 'd0f25cc40fe46416cf32b2e565dd9a5fbd979845a57297a3c78e0fc833048ba5'
$KefirizNativePaths = @($KefirizNativeFlags) + @($KefirizNativeLayouts) + @($KefirizNativePending)
$KefirizEmptySha = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
$KefirizScreens = @{
    'ResidentMenu.szs'='HOME';'Entrance.szs'='Lock Screen';'Flaunch.szs'='All Software'
    'Notification.szs'='News';'Set.szs'='System Settings';'MyPage.szs'='User Page';'Psl.szs'='Player Selection'
}

$KefirizOfficialCore = 'a6d37f1eabdc2964e2dba0984af17ebeea6de1167391118faed205d9d631d39a'
$KefirizStandaloneCores = @($KefirizOfficialCore)
if ((Get-Variable KefirizMeridianCore -ErrorAction SilentlyContinue) -and $KefirizMeridianCore -cmatch '^[0-9a-f]{64}$') {
    $KefirizStandaloneCores += $KefirizMeridianCore
}

function Test-KefirizFullFlatExport([string]$flat,[string]$preferred,[string[]]$modernBases,[string[]]$screens) {
    $added = $false
    $pairs = @()
    foreach ($screen in $screens) {
        $name = [IO.Path]::GetFileName($screen)
        $target = Child $flat $name
        $inFlat = [IO.File]::Exists($target)
        $inPreferred = [IO.File]::Exists((Child $preferred $screen))
        if ($screen.StartsWith('qlaunch/') -and (!$inFlat -or !$inPreferred)) { return $false }
        if ($inFlat -and !$inPreferred) { $added = $true }
        if ($inFlat -and (([IO.FileInfo]$target).Length -lt 16 -or ([IO.FileInfo]$target).Length -gt 64MB)) { return $false }
        foreach ($base in $modernBases) {
            $source = Child $base $screen
            if (![IO.File]::Exists($source)) { continue }
            if (!$inFlat -or ([IO.FileInfo]$source).Length -ne ([IO.FileInfo]$target).Length) { return $false }
            $pairs += [pscustomobject]@{Source=$source;Target=$target}
        }
    }
    if (!$added) { return $false }
    $digests = @{}
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($pair in $pairs) {
            foreach ($path in @($pair.Source,$pair.Target)) {
                if ($digests.ContainsKey($path)) { continue }
                $stream = [IO.File]::OpenRead($path)
                try {
                    # Check the opened, read-locked file before hashing it.
                    if ($stream.Length -lt 16 -or $stream.Length -gt 64MB) { return $false }
                    $digests[$path] = [Convert]::ToBase64String($sha.ComputeHash($stream))
                } finally { $stream.Dispose() }
            }
            if ($digests[$pair.Source] -cne $digests[$pair.Target]) { return $false }
        }
        return $true
    } finally { $sha.Dispose() }
}

function Get-KefirizNativeExport([string]$sd,[string]$hold,[bool]$markerRecovery,[ref]$newerThan) {
    $runtime = HashOf (Child $sd 'atmosphere/package3')
    $standalone = $runtime -cin $KefirizStandaloneCores
    if (!$standalone -and $runtime -cnotin @('af8e1d52814a738a5edd24ddb9e45a0d6900a68f64c0e0c1381c8dc3f857c900',
                           '1fca4f0a1d2a214d7d92c0574827066e59bbfc4e230750a800654534f1820ac2')) { return }
    $flat = Child $sd 'themes/systemData'
    $export = Child $flat 'extracted'
    $candidates = @($export,$flat)
    if ($standalone) { $candidates = @((Child $sd 'config/kefiriz/native-source/20.5.0/extracted')) + $candidates }
    $best = $null
    $most = -1
    $incompleteModern = $false
    $modernBases = @()
    $canExtend = $standalone -and !$hold -and !(Test-Path -LiteralPath (Child $sd 'config/kefiriz/native-themes/hold'))
    $screens = @('qlaunch/lyt/Flaunch.szs','qlaunch/lyt/Notification.szs','qlaunch/lyt/Set.szs',
                 'qlaunch/lyt/Entrance.szs','mypage/lyt/MyPage.szs','playerselect/lyt/Psl.szs')
    foreach ($base in $candidates) {
        if (![IO.Directory]::Exists($base)) { continue }
        $modern = $base -cne $flat
        $sentinel = Child $base '.kefiriz-reexport-required'
        if (!$modern -and ($incompleteModern -or ($best -and (!$canExtend -or (Test-Path -LiteralPath $sentinel))))) { continue }
        if ($modern -and (Test-Path -LiteralPath $sentinel)) { $canExtend=$false; continue }
        $clock = $hold
        if ($modern -and $markerRecovery) { $clock = $null }
        if (!$modern -and [IO.File]::Exists($sentinel) -and
            (!$clock -or ([IO.FileInfo]$sentinel).LastWriteTimeUtc -gt ([IO.FileInfo]$clock).LastWriteTimeUtc)) { $clock = $sentinel }
        $homeLayout = if ($modern) { Child $base 'qlaunch/lyt/ResidentMenu.szs' } else { Child $base 'ResidentMenu.szs' }
        $marker = Child $base 'ver.cfg'
        if (![IO.File]::Exists($homeLayout) -or ![IO.File]::Exists($marker) -or ([IO.FileInfo]$marker).Length -gt 64 -or
            [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($marker)).TrimEnd([char]13,[char]10) -cne '20.5.0') {
            if ($modern) { $incompleteModern = $true }
            continue
        }
        if (!$modern -and $best) {
            # All five qlaunch archives (including stock Entrance) must agree before
            # taking extra applet screens from ONE complete unheld flat export.
            if (Test-KefirizFullFlatExport $base $best $modernBases (@('qlaunch/lyt/ResidentMenu.szs') + $screens)) { $best=$base }
            continue
        }
        $inputs = @($marker,$homeLayout)
        foreach ($screen in $screens) {
            if ($modern -and $standalone -and $screen -ceq 'qlaunch/lyt/Entrance.szs') { continue }
            $file = if ($modern) { Child $base $screen } else { Child $base ([IO.Path]::GetFileName($screen)) }
            if ([IO.File]::Exists($file)) { $inputs += $file }
        }
        if ($clock -and @($inputs | Where-Object { ([IO.FileInfo]$_).LastWriteTimeUtc -le ([IO.FileInfo]$clock).LastWriteTimeUtc }).Count) { continue }
        if ($modern) { $modernBases += $base }
        if ($inputs.Count -gt $most) {
            $best = $base
            $most = $inputs.Count
            if ($null -ne $newerThan) { $newerThan.Value = $clock }
        }
    }
    return $best
}

function Write-KefirizNativeScreens($adaptive,[bool]$stockLock) {
    $names = @($KefirizNativeLayouts | Where-Object { $adaptive.ContainsKey($_) } |
        ForEach-Object { $KefirizScreens[[IO.Path]::GetFileName($_)] })
    Write-Host ("Screens using the system palette: " + ($names -join ', ') + '.')
    if ($stockLock) { Write-Host 'Standalone uses the system lock screen.' }
    $missing = @($KefirizScreens.Values | Where-Object { $_ -cnotin $names -and !($stockLock -and $_ -ceq 'Lock Screen') } | Sort-Object)
    if ($missing.Count) {
        Write-Host ("Remaining screens: " + ($missing -join ', ') + '.')
        Write-Host 'Choose Apply artwork after exporting the missing resources to add these screens.'
    }
}

function Clear-KefirizNativePreparation([string]$work) {
    if (![IO.Directory]::Exists($work)) { return }
    for ($i = 0; $i -lt $KefirizNativePaths.Count; $i++) {
        foreach ($suffix in '.old','.new') {
            $p = Child $work ("$i" + $suffix)
            if ([IO.File]::Exists($p)) { [IO.File]::Delete($p) }
        }
    }
    $receipt = Child $work 'receipt.json'
    if ([IO.File]::Exists($receipt)) { [IO.File]::Delete($receipt) }
    # Only remove an empty directory. Preserve unexpected contents for inspection.
    [IO.Directory]::Delete($work)
}

function Read-KefirizNativeReceipt([string]$journal) {
    $r = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText((Child $journal 'receipt.json')))
    if ($r.Format -ne 1 -or $r.Edition -cne 'KEFIRIZ-HOME-786' -or
        $r.Palette -cnotin @('Dark','Light','System') -or !$r.Files.Count -or
        $r.Files.Count -gt $KefirizNativePaths.Count) { throw 'Invalid native HOME receipt.' }
    $previous = -1
    $seenHome = $false
    foreach ($row in $r.Files) {
        $i = [array]::IndexOf($KefirizNativePaths,$row.Path)
        if ($i -le $previous -or $row.Original -isnot [string] -or $row.Installed -isnot [string] -or
            ($row.Original -and $row.Original -cnotmatch '^[0-9a-f]{64}$')) {
            throw 'Invalid native HOME receipt entry.'
        }
        $previous = $i
        if ($row.Path -ceq $KefirizNativePending) {
            if ($row.Installed -cne '' -or $row.Original -cne $KefirizPendingSha) { throw 'Invalid theme handoff receipt.' }
        } elseif ($row.Path -cin $KefirizNativeFlags) {
            if ($row.Installed -cne $KefirizEmptySha -or $row.Original) { throw 'Invalid native resource flag receipt.' }
        } elseif ($row.Installed -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid native resource digest.' }
        if ($row.Original -and $row.Original -cne $row.Installed -and
            (HashOf (Child $journal "$i.old")) -cne $row.Original) {
            throw "The original backup failed verification: $($row.Path)"
        }
        if ($row.Path -ceq $KefirizNativeLayouts[-1]) { $seenHome = $true }
    }
    if (!$seenHome) { throw 'Native receipt is missing Nintendo HOME.' }
    return $r
}

function Restore-KefirizNativeReceipt([string]$sd,[string]$journal,$r,[switch]$Strict) {
    $compatible = [bool](Get-KefirizNativeExport $sd)
    if ($Strict -and !$compatible) { throw 'Undo requires a matching 20.5.0 HOME export on the card.' }
    if (!$compatible) { Write-Host 'The system base has changed. Previous theme archives will remain in the backup.' }
    Folder (Child $journal 'restoring')
    $originals = @{}
    foreach ($file in $r.Files) {
        if ($file.Path -cin $KefirizNativeLayouts -and $file.Original) {
            $originals[(Child $sd $file.Path)] = $file.Original
        }
    }
    for ($n = $r.Files.Count-1; $n -ge 0; $n--) {
        $row = $r.Files[$n]
        if ($row.Original -ceq $row.Installed) { continue }
        $p = Child $sd $row.Path
        $live = HashOf $p
        if ($live -ceq $row.Original) { continue }
        if ($live -cne $row.Installed) {
            if ($Strict) { throw "Native artwork changed while undoing the revision: $($row.Path)" }
            Write-Host "Preserved a later change: $($row.Path)"; continue
        }
        if (!$compatible -and $row.Path -ceq $KefirizNativePending) { continue }
        if (!$row.Original -or (!$compatible -and $row.Path -cin $KefirizNativeLayouts)) {
            if ($row.Path -cin $KefirizNativeFlags) {
                # This flag may now serve a new theme or other resources in the same title.
                $romfs = Child ([IO.Path]::GetDirectoryName($p)) 'romfs'
                $shared = [IO.Directory]::Exists($romfs) -and
                    (Get-ChildItem -LiteralPath $romfs -Recurse -File -Force | Where-Object {
                        !$originals.ContainsKey($_.FullName) -or
                        (HashOf $_.FullName) -cne $originals[$_.FullName]
                    } | Select-Object -First 1)
                if ($shared) {
                    Write-Host "Kept resource flag in use: $($row.Path)"
                    continue
                }
            }
            if ((HashOf $p) -cne $row.Installed) { throw 'A native resource changed during restore.' }
            [IO.File]::Delete($p)
        } else {
            $i = [array]::IndexOf($KefirizNativePaths,$row.Path)
            $staged = Child $journal ('restore-' + [guid]::NewGuid().ToString('N'))
            try {
                CopyChecked (Child $journal "$i.old") $staged $row.Original
                Commit $staged $p $live $row.Original
            } finally { if ([IO.File]::Exists($staged)) { [IO.File]::Delete($staged) } }
        }
    }
    $root = [IO.Path]::GetDirectoryName($journal)
    $archived = Child $root ('restored-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::Move($journal,$archived)
    Write-Host "Native HOME files restored. Backup: $archived"
}

function New-KefirizNativeReceipt([string]$sd,[string]$work,[string]$journal,[string]$palette,$outputs) {
    Folder $work
    try {
        $rows = foreach ($rel in $KefirizNativePaths) {
            if (!$outputs.ContainsKey($rel)) { continue }
            $live = Child $sd $rel
            $old = HashOf $live
            $src = $outputs[$rel]
            $i = [array]::IndexOf($KefirizNativePaths,$rel)
            if ($rel -cin $KefirizNativeFlags -and $old) { continue }
            if ($rel -ceq $KefirizNativePending -and $old -cne $KefirizPendingSha) { continue }
            $want = if ($rel -ceq $KefirizNativePending) { '' } else { HashOf $src }
            if ($rel -cne $KefirizNativePending -and !$want) { throw "Missing prepared native file: $rel" }
            if ($old -cne $want) {
                if ($old) { CopyChecked $live (Child $work "$i.old") $old }
                if ($want) { CopyChecked $src (Child $work "$i.new") $want }
            }
            [pscustomobject]@{Path=$rel;Original=$old;Installed=$want}
        }
        $receipt = [pscustomobject]@{Format=1;Edition='KEFIRIZ-HOME-786';Palette=$palette;Files=@($rows)}
        WriteReceipt (Child $work 'receipt.json') $receipt
        [void](Read-KefirizNativeReceipt $work)
        [IO.Directory]::Move($work,$journal)
    } catch {
        Clear-KefirizNativePreparation $work
        throw
    }
}

function Install-KefirizNativeReceipt([string]$SdRoot,[string]$journal,$receipt) {
    try {
        foreach ($row in $receipt.Files) {
            $p = Child $SdRoot $row.Path
            $live = HashOf $p
            if ($live -ceq $row.Installed) { continue }
            if ($live -cne $row.Original) { throw "Native resources changed during installation: $($row.Path)" }
            $i = [array]::IndexOf($KefirizNativePaths,$row.Path)
            if ($row.Path -ceq $KefirizNativePending) { [IO.File]::Delete($p) }
            else {
                $staged = Child $journal "$i.new"
                if ((HashOf $staged) -cne $row.Installed) { throw "Staged native resource failed verification: $($row.Path)" }
                Commit $staged $p $live $row.Installed
            }
        }
        Folder (Child $journal 'installed')
    } catch {
        $cause = $_.Exception.Message
        Restore-KefirizNativeReceipt $SdRoot $journal $receipt
        throw "Native HOME installation rolled back. $cause"
    }
}

function Get-KefirizNativeJournals([string]$root) {
    if (![IO.Directory]::Exists($root)) { return }
    $count = 0
    $active = Child $root 'active'
    if ([IO.Directory]::Exists($active)) { $active; $count++ }
    foreach ($dir in @(Get-ChildItem -LiteralPath $root -Directory -Force |
        Where-Object Name -CLike 'revision-*' | Sort-Object Name)) {
        $expected = 'revision-{0:d4}' -f $count
        if (!$count -or $dir.Name -cne $expected) { throw 'Incomplete native HOME history.' }
        Child $root $expected
        $count++
    }
}

function Undo-KefirizNativeRevision([string]$sd,[string]$root) {
    if (Test-Path -LiteralPath (Child $sd 'config/kefiriz/native-themes/hold')) {
        throw 'Theme recovery is active. Finish recovery before undoing an artwork revision.'
    }
    $journals = @(Get-KefirizNativeJournals $root)
    if ($journals.Count -lt 2) {
        Write-Host 'No previous managed artwork revision is available. The initial artwork stays installed.'
        return
    }
    $latest = $journals[-1]
    $previous = @{}
    foreach ($journal in $journals) {
        if (![IO.Directory]::Exists((Child $journal 'installed')) -or
            ($journal -cne $latest -and [IO.Directory]::Exists((Child $journal 'restoring')))) {
            throw 'Artwork history has an incomplete operation. Finish recovery before undoing this revision.'
        }
        $receipt = Read-KefirizNativeReceipt $journal
        if ($journal -ceq $latest) { break }
        foreach ($row in $receipt.Files) { $previous[$row.Path] = $row.Installed }
    }
    # Validate the whole managed state before the first replacement. Only an
    # interrupted restore may already have an original file at its live path.
    $expected = $previous.Clone()
    $resuming = [IO.Directory]::Exists((Child $latest 'restoring'))
    foreach ($row in $receipt.Files) {
        if ($row.Path -cne $KefirizNativePending -and $previous.ContainsKey($row.Path) -and
            $previous[$row.Path] -cne $row.Original) { throw "Conflicting artwork history: $($row.Path)" }
        $expected[$row.Path] = $row.Installed
        if ($resuming -and (HashOf (Child $sd $row.Path)) -ceq $row.Original) {
            $expected[$row.Path] = $row.Original
        }
    }
    foreach ($path in $expected.Keys) {
        if ((HashOf (Child $sd $path)) -cne $expected[$path]) { throw "Modified native artwork was preserved: $path" }
    }
    Restore-KefirizNativeReceipt $sd $latest $receipt -Strict
    Write-Host 'The previous native artwork revision is active.'
}

function Invoke-KefirizNativeTheme {
    param([Parameter(Mandatory)][ValidateSet('Install','Restore')][string]$Action,
          [Parameter(Mandatory)][string]$SdRoot,[Parameter(Mandatory)][string]$PackRoot,
          [ValidateSet('Dark','Light')][string]$Palette,[switch]$RetryAfterRecovery,
          [switch]$Standalone,[switch]$Automatic)
    $SdRoot = (Get-Item -LiteralPath $SdRoot).FullName
    $hold = Child $SdRoot 'config/kefiriz/native-themes/hold'
    $retry = $Action -eq 'Install' -and $RetryAfterRecovery -and [IO.File]::Exists($hold)
    if ((Test-Path -LiteralPath $hold) -and !$retry) {
        Write-Host 'Theme recovery is active. Native graphics and their backup history were left untouched.'
        return $false
    }
    $root = if ($Standalone) { Child $SdRoot 'config/kefiriz/standalone-install/active/native-artwork' }
            else { Child $SdRoot 'config/kefiriz/distribution-installer/native' }
    $active = Child $root 'active'
    $work = Child $root 'preparing'
    if (!$retry) { Clear-KefirizNativePreparation $work }
    $journals = @()
    if (!$retry) { $journals = @(Get-KefirizNativeJournals $root) }
    if ($Action -eq 'Restore') {
        # The original receipt is always unwound last, including revision 2 installs.
        foreach ($journal in $journals) { [void](Read-KefirizNativeReceipt $journal) }
        for ($i = $journals.Count - 1; $i -ge 0; $i--) {
            $journal = $journals[$i]
            Restore-KefirizNativeReceipt $SdRoot $journal (Read-KefirizNativeReceipt $journal)
        }
        return ($journals.Count -gt 0)
    }

    $recovery = if ($retry) { $hold }
    $markerRecovery = $retry -and ([IO.FileInfo]$hold).Length -eq 12 -and [IO.File]::ReadAllText($hold) -ceq 'nx-export-v2'
    $clock = $null
    $base = Get-KefirizNativeExport $SdRoot $recovery $markerRecovery ([ref]$clock)
    if (!$base) {
        Write-Host 'Native artwork needs a current 20.5.0 HOME export. Open KEFIRIZ System on the console, then run the installer again.'
        return $false
    }
    $stockLock = (HashOf (Child $SdRoot 'atmosphere/package3')) -cin $KefirizStandaloneCores
    $current = @{}
    $adaptive = @{}
    $owners = $journals
    if ($Standalone -and !$retry -and !$journals.Count -and [IO.File]::Exists((Child $SdRoot $KefirizNativeLayouts[-1]))) {
        # Aurora 2.1.0 stored manual artwork receipts in the legacy directory.
        # Read their proven ownership only; new revisions and rollback copies belong to this installation.
        $owners = @(Get-KefirizNativeJournals (Child $SdRoot 'config/kefiriz/distribution-installer/native'))
    }
    foreach ($journal in $owners) {
        $r = Read-KefirizNativeReceipt $journal
        if ([IO.Directory]::Exists((Child $journal 'restoring'))) { throw 'A native theme restore was interrupted. Run Restore again.' }
        if (![IO.Directory]::Exists((Child $journal 'installed'))) {
            Write-Host 'A previous theme installation did not finish. Use theme recovery before applying new artwork.'
            return $false
        }
        foreach ($row in $r.Files) {
            $current[$row.Path] = $row.Installed
            if ($r.Palette -ceq 'System' -and $row.Path -cin $KefirizNativeLayouts) { $adaptive[$row.Path] = $true }
        }
    }
    foreach ($rel in $current.Keys) {
        $live = HashOf (Child $SdRoot $rel)
        if ($rel -cne $KefirizNativePending -and $live -cne $current[$rel]) {
            Write-Host "The active theme has changed since installation. Preserved $rel."
            return $false
        }
    }

    foreach ($folder in @($KefirizNativeLayouts | ForEach-Object { $_.Substring(0,$_.LastIndexOf('/')) } | Sort-Object -Unique)) {
        $lyt = Child $SdRoot $folder
        if (![IO.Directory]::Exists($lyt)) { continue }
        foreach ($file in [IO.Directory]::EnumerateFiles($lyt, '*.szs')) {
            $rel = $folder + '/' + [IO.Path]::GetFileName($file)
            if (!$current.ContainsKey($rel)) {
                Write-Host "A separately installed native layout is active: $rel. Theme recovery can move it to a backup."
                return $false
            }
        }
    }
    $renderer = Child $PackRoot 'native-render/runtime/Render.exe'
    if (!$Standalone -and ![IO.File]::Exists($renderer)) { $renderer = Child $PackRoot 'native-render/Render.exe' }
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    $scratch = Child $tempRoot ('kefiriz-native-' + [guid]::NewGuid().ToString('N'))
    Folder $scratch
    try {
        $dest = Child $scratch 'output'
        Folder $dest
        $support = Child $SdRoot 'atmosphere/exefs_patches'
        # Both legacy palette arguments now use the system's own runtime material colors.
        $renderArgs = @('--base',$base,'--palette','Light','--output',$dest)
        if ($stockLock) { $renderArgs += '--stock-lock','yes' }
        elseif ([IO.Directory]::Exists($support)) { $renderArgs += '--lock-support',$support }
        if ($clock) { $renderArgs += '--newer-than',$clock }
        $paths = @(& $renderer @renderArgs)
        $rc = $LASTEXITCODE
        if ($rc -eq 2) {
            Write-Host 'No compatible local HOME export was found. Installation can continue with the current system artwork.'
            return $false
        }
        if ($Automatic -and $rc -eq 1) {
            Write-Warning 'The native renderer rejected this export. Existing system graphics were preserved.'
            return $false
        }
        if ($rc -ne 0) { throw "Native renderer returned $rc. The active HOME files were preserved." }
        $flag = Child $scratch 'empty.flag'
        [IO.File]::WriteAllBytes($flag,[byte[]]@())
        $outputs = @{}
        foreach ($rel in $paths) {
            if ($rel -cnotin $KefirizNativeLayouts -or $outputs.ContainsKey($rel)) { throw 'Unexpected native renderer output.' }
            if ($stockLock -and [IO.Path]::GetFileName($rel) -ceq 'Entrance.szs') { throw 'Standalone requires the system lock screen.' }
            $file = Child $dest $rel
            if (![IO.File]::Exists($file) -or ([IO.FileInfo]$file).Length -lt 16) { throw "Incomplete rendered file: $rel" }
            $outputs[$rel] = $file
            $titleFlag = $rel.Substring(0,$rel.IndexOf('/romfs/')) + '/fsmitm.flag'
            $outputs[$titleFlag] = $flag
        }
        if (!$outputs.ContainsKey($KefirizNativeLayouts[-1])) { throw 'The renderer did not produce Nintendo HOME.' }
        foreach ($oldLayout in $adaptive.Keys) {
            if (!$outputs.ContainsKey($oldLayout)) {
                Write-Host 'An active screen no longer has its matching export or support file. Use theme recovery before applying artwork.'
                return $false
            }
        }
        if ($adaptive.Count -eq $paths.Count -and !@($paths | Where-Object {
            !$adaptive.ContainsKey($_) -or $current[$_] -cne (HashOf $outputs[$_])
        }).Count) { Write-KefirizNativeScreens $adaptive $stockLock; return $true }
        $outputs[$KefirizNativePending] = ''
        if ($retry -and [IO.Directory]::Exists($root)) {
            $archived = Child ([IO.Path]::GetDirectoryName($root)) ('native-before-recovery-' + [guid]::NewGuid().ToString('N'))
            [IO.Directory]::Move($root,$archived)
            Write-Host "Previous native theme history: $archived"
        }
        $journal = if ($journals.Count) { Child $root ('revision-{0:d4}' -f $journals.Count) } else { $active }
        New-KefirizNativeReceipt $SdRoot $work $journal 'System' $outputs
    } finally {
        if ([IO.Directory]::Exists($scratch) -and [IO.Path]::GetDirectoryName($scratch) -eq $tempRoot -and
            [IO.Path]::GetFileName($scratch) -match '^kefiriz-native-[0-9a-f]{32}$') {
            Remove-Item -LiteralPath $scratch -Recurse -Force
        }
    }
    Install-KefirizNativeReceipt $SdRoot $journal (Read-KefirizNativeReceipt $journal)
    if ($retry) { [IO.File]::Delete($hold) }
    foreach ($rel in $paths) { $adaptive[$rel] = $true }
    Write-KefirizNativeScreens $adaptive $stockLock
    return $true
}
