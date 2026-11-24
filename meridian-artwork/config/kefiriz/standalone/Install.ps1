param(
    [ValidateSet('Install','Restore','Artwork','Undo-Artwork','Recover','Verify','Download','DBI','Profiles','Services')][string]$Action = 'Install',
    [string]$SdRoot
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'CardFiles.ps1')
. (Join-Path $PSScriptRoot 'MeridianBoot.ps1')
$artworkOnly = $Action -in @('Artwork','Undo-Artwork')
if (!$artworkOnly) { . (Join-Path $PSScriptRoot 'BootArtwork.ps1') }
$bundle = Join-Path $PSScriptRoot 'bundle'
$tools = Join-Path (Split-Path $PSScriptRoot) 'distribution'
$runtime = 'a6d37f1eabdc2964e2dba0984af17ebeea6de1167391118faed205d9d631d39a'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$themesPath = 'switch/NxThemesInstaller/NxThemesInstaller.nro'
$themesHash = '36ac0fac5477e08a176b2f155b8b3c170c1291004d943541348599bae8cf97a1'

$updatePaths = @(
    'hbmenu.nro',
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro',
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro',
    'atmosphere/contents/42000000004B4650/exefs.nsp',
    'atmosphere/contents/42000000004B4654/exefs.nsp',
    'bootloader/res/KEFIRIZ-boot.bmp',
    'bootloader/res/KEFIRIZ-CFW.bmp',
    'config/kefiriz/release.txt'
)
$previousApps = @{
    'hbmenu.nro'='be59842f4780f5b9a641c9af801df247efd6e94666e07995894175313a6182c0'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='9f097a33f9dfb565c84c82f4396e2bd0c256d82350350fc9b73dfd1c343e50af'
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro'='208bd6042f1f37169da2ba75259334e91128201d4d6fec3c68d89517abac5358'
    'atmosphere/contents/42000000004B4650/exefs.nsp'='61aff6623d884cab86a2d7041b66ffeaa27092af8c467cc928ec6cb471e928ee'
    'atmosphere/contents/42000000004B4654/exefs.nsp'='c4922de663e7b7854f474bac058690995238184aba55fc1d4a8d54791c205193'
    'bootloader/res/KEFIRIZ-boot.bmp'='6154c484d8851201b13a0c932b10a4f8667242ebec89e4970958d77986ac628f'
    'bootloader/res/KEFIRIZ-CFW.bmp'='9def62a0cb8052d8b89948d533fc570698f9a3cb3d306201a3e074b5fc77c2a6'
    'config/kefiriz/release.txt'='2a3b15ad93511da18739fb0685cd02e05a506b524e76559af535777c950c8e06'
}
$intermediateApps = @{
    'hbmenu.nro'='9cf669b3a0c228803d0e0a9f5227b6f5b5f6b4db696b79c2ae56b9b4c0851bd3'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='b2f649746844fd965a63029d32bde40d7cb9b635017f8def36acc42feacdf675'
    'config/kefiriz/release.txt'='e6c0210c57c8dfc8790b29abf4c10a4a397b3647df45b1558a79ffca9cb13680'
}
$repairApps = @{
    'hbmenu.nro'='ffd13fa442a43a550bc7421d7a0ff4427457dc0e1b950599777ee208e6e73574'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='2fc0802f88d598b3921b1f74132f06419d26ab5021ae5558f9e5df3aa5850ec9'
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro'='e516145920d660b3d317ad6ccb4dfed782c8aa34a4c89716b58a94d761efac81'
    'atmosphere/contents/42000000004B4650/exefs.nsp'='67769c52f767bef031889baae7f65d767e9eb8c79514f2c93c375d50056c2346'
    'atmosphere/contents/42000000004B4654/exefs.nsp'='02dd442cfd441675f54a4e78d38ad410027cd991b15abfb71ef8ec1034af2037'
    'bootloader/res/KEFIRIZ-boot.bmp'='70cdaea32588eb2484f7972dd8f229359de3ba9f177d1b43c320154d965084e1'
    'bootloader/res/KEFIRIZ-CFW.bmp'='915a37ef789fffea7c75216c4f01fd023988074bc7fc4f898265b0840f8937ce'
    'config/kefiriz/release.txt'='88b62f6398215ca29a66e8029958edd99a31dede227770089e7d8464a52aee21'
}
$bootstrapApps = @{
    'hbmenu.nro'='9763a60cf5140d0c5e52890833a33530a308a01a73c55a3b0e6e999013828b6e'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='95ea3c70752408c0e7d7aafd689ae976e95fa84f32648a7c4bfe12cf147538c4'
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro'='d602fe3d44d09bf5eaf8b285b882e54e9ca4c56746164270c4f7cc4c7266f103'
    'config/kefiriz/release.txt'='311fcf570bb2ae47ee61c88f5db60e3ce7b77a34c14938925d008b86dede03b2'
}
$aurora210Apps = @{
    'hbmenu.nro'='85e70ead9fd29843100ea913d766c8ec3cd3f631f498416f391f2d3aafeef443'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='95ea3c70752408c0e7d7aafd689ae976e95fa84f32648a7c4bfe12cf147538c4'
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro'='453c74f1e19111d43717d38bd9702cb1f34fde99d457717bafa356e84731f300'
    'atmosphere/contents/42000000004B4650/exefs.nsp'='90d294b6c7edda4492aea5698acf263840211c0cb4cd4aacbaf4900e851c7a4a'
    'atmosphere/contents/42000000004B4654/exefs.nsp'='cc26b4bbce60969f650ce3c543ab612f299f20864fd7d4c78029159dcb4fb117'
    'bootloader/res/KEFIRIZ-boot.bmp'='61d714c98ace163e43c07a38e241a8daf2b1232b5b26f1d5dba10900f0abdde7'
    'bootloader/res/KEFIRIZ-CFW.bmp'='4c0e77177403b5d574cc3a763838357a98428877139670199d03ed9b6c99553f'
    'config/kefiriz/release.txt'='904a69e031e96123090c0b3326edd2753d9f2f712af0dc4a9f593ce8a44cdfbf'
}
$aurora211Apps = @{
    'hbmenu.nro'='71447f9f76f7a31141cd2d53632b96af263b0eb02c830e6c2850f02d062bf6ab'
    'switch/KEFIRJS-Updater/KEFIRJS-Updater.nro'='0503fd8f28a7041e9b2e5becd2ba57959c6f962f064c504cec9bb22662f85550'
    'switch/KEFIRIZ-System/KEFIRIZ-System.nro'='2cd5b28fdd8f491f147a7a5c540d34b5eefa344191d20c07f885ba3ae39f0845'
    'atmosphere/contents/42000000004B4650/exefs.nsp'='6e56bdc65af6da093724d93419ad9f60e3f66da7f820d1072a39b2d85797d0a2'
    'config/kefiriz/release.txt'='166ad12edd2206a9e3b8a61847e8492c9d8a3f82eba10f9dae019ec4935534e7'
}
function Previous-Asset($name,$digest) {
    return (($previousApps.ContainsKey($name) -and $digest -ceq $previousApps[$name]) -or
            ($intermediateApps.ContainsKey($name) -and $digest -ceq $intermediateApps[$name]) -or
            ($repairApps.ContainsKey($name) -and $digest -ceq $repairApps[$name]) -or
            ($bootstrapApps.ContainsKey($name) -and $digest -ceq $bootstrapApps[$name]) -or
            ($aurora210Apps.ContainsKey($name) -and $digest -ceq $aurora210Apps[$name]) -or
            ($aurora211Apps.ContainsKey($name) -and $digest -ceq $aurora211Apps[$name]))
}
function Missing-ServiceAllowed($name) {
    return $name -cin @('atmosphere/contents/42000000004B4650/exefs.nsp','atmosphere/contents/42000000004B4654/exefs.nsp')
}

function Read-Bundle {
    $json = Child $bundle 'manifest.json'
    $pin = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'manifest.sha256')).Trim()
    if ($pin -cnotmatch '^[0-9a-f]{64}$' -or (HashOf $json) -cne $pin) { throw 'The offline package manifest failed verification.' }
    $m = [IO.File]::ReadAllText($json) | ConvertFrom-Json
    $entries = @{}
    foreach ($row in $m.files) {
        $path = $row.path
        if ($path -cnotmatch '^[a-zA-Z0-9._/-]+$' -or $path -match '(^/|/$|//|(^|/)\.{1,2}(/|$))' -or
            $path -notmatch '^(atmosphere/|bootloader/|config/|switch/|themes/|hbmenu\.nro$)' -or
            $entries.ContainsKey($path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Unexpected file in the offline manifest.' }
        $f = Child $bundle ('sd/' + $path)
        if (!(Test-Path -LiteralPath $f -PathType Leaf) -or ([IO.FileInfo]$f).Length -ne $row.size -or (HashOf $f) -cne $row.sha256) { throw "Damaged distribution file: $path" }
        $entries[$path] = $row.sha256
    }
    if ($entries['atmosphere/package3'] -cne $runtime -or !$entries.ContainsKey('bootloader/ini/zzzz-KEFIRIZ.ini')) { throw 'Unexpected Atmosphere edition.' }
    [void](Test-KefirizMeridianCore $bundle)
    return $entries
}
function Test-ArtworkTools {
    $records = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'artwork-integrity.json')) | ConvertFrom-Json
    foreach ($row in $records.PSObject.Properties) {
        if ($row.Name -cnotmatch '^(NativeTheme\.ps1|native-render/runtime/[a-zA-Z0-9._/-]+)$' -or
            $row.Name -match '(^|/)\.{1,2}(/|$)' -or (HashOf (Child $tools $row.Name)) -cne $row.Value) {
            throw "Damaged artwork tool: $($row.Name)"
        }
    }
    if (!$records.'native-render/runtime/Render.exe' -or !$records.'native-render/runtime/Newtonsoft.Json.dll' -or !$records.'NativeTheme.ps1') { throw 'Incomplete artwork tool manifest.' }
}
function CheckPath([string]$root,[string]$path) {
    $prefix = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
    if (![IO.Path]::GetFullPath($path).StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "Path outside $root" }
}
function MoveDirectory([string]$from,[string]$to) {
    CheckPath $SdRoot $from
    CheckPath $SdRoot $to
    [IO.Directory]::Move($from,$to)
}
function Recover-Layouts([switch]$OnlyIfActive,[IO.FileStream]$DistributionLock) {
    & (Join-Path $tools 'RecoverTheme.ps1') -SdRoot $SdRoot -OnlyIfActive:$OnlyIfActive -DistributionLock $DistributionLock
    if ($LASTEXITCODE -ne 0) { throw 'Theme recovery did not finish. Leave the card connected and retry.' }
}
function Get-PublishedArchive([string]$destination,[string]$name) {
    $repository = 'Stasan-minecraft/kefirjs-updates'
    $tag = 'atmosphere-v2.2.0'
    $url = "https://github.com/$repository/releases/download/$tag/$name"
    $release = Invoke-RestMethod -UseBasicParsing -TimeoutSec 30 -UserAgent 'KEFIRIZ-Installer' -Uri "https://api.github.com/repos/$repository/releases/tags/$tag"
    $assets = @($release.assets | Where-Object { $_.name -ceq $name -and $_.browser_download_url -ceq $url })
    if ($assets.Count -ne 1 -or $assets[0].digest -cnotmatch '^sha256:[0-9a-f]{64}$' -or
        $assets[0].size -le 0 -or $assets[0].size -gt 256MB) { throw 'The release does not provide a verified installer archive.' }
    $digest = $assets[0].digest.Substring(7)
    if ([IO.File]::Exists($destination)) {
        if ((HashOf $destination) -cne $digest) { throw 'An unrelated ZIP already exists at the download destination.' }
        return
    }
    $part = Child ([IO.Path]::GetDirectoryName($destination)) ('download-' + [guid]::NewGuid().ToString('N') + '.part')
    try {
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 180 -UserAgent 'KEFIRIZ-Installer' -Uri $url -OutFile $part
        if (([IO.FileInfo]$part).Length -ne $assets[0].size -or (HashOf $part) -cne $digest) { throw 'The downloaded installer failed verification.' }
        [IO.File]::Move($part,$destination)
    } finally { if ([IO.File]::Exists($part)) { [IO.File]::Delete($part) } }
}
function Clear-PreparedTree([string]$dir) {
    foreach ($entry in Get-ChildItem -LiteralPath $dir -Force) {
        $item = Child $dir $entry.Name
        CheckPath $state $item
        if ($entry.PSIsContainer) { Clear-PreparedTree $item } else { [IO.File]::Delete($item) }
    }
    [IO.Directory]::Delete($dir)
}
function Clear-AbandonedStages {
    foreach ($dir in Get-ChildItem -LiteralPath $state -Directory -Force) {
        if ($dir.Name -cnotmatch '^stage-[0-9a-f]{32}$') { continue }
        $abandoned = Child $state $dir.Name
        $owner = Child $abandoned 'stage.owner'
        if ([IO.File]::Exists($owner) -and [IO.File]::ReadAllText($owner) -ceq 'KEFIRIZ-ATM-PREP-1') {
            CheckPath $state $abandoned
            Clear-PreparedTree $abandoned
        }
    }
}
function New-Stage {
    # All replacements and original file copies are staged before the first runtime rename.
    $scratch = Child $state ('stage-' + [guid]::NewGuid().ToString('N'))
    Folder $scratch
    $rows = @()
    # Mark disposable prep before copying the first file; abandoned preparations can be reclaimed.
    [IO.File]::WriteAllText((Child $scratch "stage.owner"), "KEFIRIZ-ATM-PREP-1")
    $hadAtm = [IO.Directory]::Exists($atm)
    foreach ($path in ($files.Keys | Sort-Object)) {
        $original = Child $SdRoot $path
        $inputFile = Child $bundle ('sd/' + $path)
        $desired = $files[$path]
        if ($path -cin @('atmosphere/contents/42000000004B4650/flags/boot2.flag','atmosphere/contents/42000000004B4654/flags/boot2.flag')) {
            $module = $path.Replace('flags/boot2.flag','exefs.nsp')
            if ([IO.File]::Exists((Child $SdRoot $module)) -and ![IO.File]::Exists($original)) { continue }
        }
        if ($path.StartsWith('atmosphere/')) {
            CopyChecked $inputFile (Child $scratch ('new-' + $path)) $desired
            continue
        }
        $old = HashOf $original
        if ($old -and $path -in @('switch/TorrentShopNX/config.ini','config/sys-clk/config.ini')) { continue }
        if ($old -ceq $desired) { continue }
        $index = $rows.Count
        if ($old) { CopyChecked $original (Child $scratch ("original-$index")) $old }
        CopyChecked $inputFile (Child $scratch ("new-$index")) $desired
        $rows += [pscustomobject]@{Path=$path;Original=$old;Installed=$desired}
    }
    foreach ($path in $retired.Keys) {
        $original = Child $SdRoot $path
        $old = HashOf $original
        if (!$old -or $old -cnotin $retired[$path]) { continue }
        CopyChecked $original (Child $scratch ("original-" + $rows.Count)) $old
        $rows += [pscustomobject]@{Path=$path;Original=$old;Installed=''}
    }
    # Keep per-console network settings, including user DNS exclusions.
    if ($hadAtm) {
        $settings = @('config/system_settings.ini','config/stratosphere.ini')
        $hosts = Child $atm 'hosts'
        if ([IO.Directory]::Exists($hosts)) {
            foreach ($f in Get-ChildItem -LiteralPath $hosts -File -Force) { $settings += 'hosts/' + $f.Name }
        }
        foreach ($rel in $settings) {
            $file = Child $atm $rel
            if (![IO.File]::Exists($file)) { continue }
            $target = Child $scratch ('new-atmosphere/' + $rel)
            if (![IO.File]::Exists($target)) { CopyChecked $file $target (HashOf $file) }
        }
    }
    WriteReceipt (Child $scratch 'receipt.json') ([pscustomobject]@{Version=1;HadAtmosphere=$hadAtm;Rows=@($rows)})
    MoveDirectory $scratch $active
}
function Open-Receipt {
    $r = [IO.File]::ReadAllText((Child $active 'receipt.json')) | ConvertFrom-Json
    if ($r.Version -ne 1 -or $r.HadAtmosphere -isnot [bool] -or $null -eq $r.Rows) { throw 'Invalid installation receipt.' }
    $seen = @{}
    for ($i=0; $i -lt $r.Rows.Count; $i++) {
        $row = $r.Rows[$i]
        $replacement = $row.Path -and $files.ContainsKey($row.Path) -and !$row.Path.StartsWith('atmosphere/') -and
            ($row.Installed -ceq $files[$row.Path] -or (Previous-Asset $row.Path $row.Installed))
        $removal = $row.Path -and $retired.ContainsKey($row.Path) -and $row.Installed -ceq '' -and $row.Original -cin $retired[$row.Path]
        $previousThemesInstall = $row.Path -ceq $themesPath -and $row.Installed -ceq $themesHash
        if ((!$replacement -and !$removal -and !$previousThemesInstall) -or $seen.ContainsKey($row.Path) -or $row.Original -isnot [string] -or
            ($row.Original -and ($row.Original -cnotmatch '^[0-9a-f]{64}$' -or (HashOf (Child $active "original-$i")) -cne $row.Original))) {
            throw 'Invalid or incomplete card backup.'
        }
        $seen[$row.Path]=$true
    }
    return $r
}
function Install-Prepared($r) {
    $new = Child $active 'new-atmosphere'
    $old = Child $active 'previous-atmosphere'
    if ([IO.Directory]::Exists($new)) {
        if ($r.HadAtmosphere -and ![IO.Directory]::Exists($old)) { MoveDirectory $atm $old }
        if ([IO.Directory]::Exists($atm)) { throw 'An unexpected atmosphere directory appeared during installation.' }
        MoveDirectory $new $atm
    }
    if ((HashOf (Child $atm 'package3')) -cnotin @($runtime,$KefirizMeridianCore)) { throw 'The installed Atmosphere core did not verify.' }
    for ($i=0; $i -lt $r.Rows.Count; $i++) {
        $row = $r.Rows[$i]
        $target = Child $SdRoot $row.Path
        $actual = HashOf $target
        if ($actual -ceq $row.Installed) { continue }
        if (!$row.Installed) {
            if ($actual -cne $row.Original) { throw "A legacy file changed: $($row.Path)" }
            [IO.File]::Move($target,(Child $active "retired-$i"))
            continue
        }
        $source = Child $active "new-$i"
        if ($actual -cne $row.Original -or (HashOf $source) -cne $row.Installed) { throw "An installation file changed: $($row.Path)" }
        Commit $source $target $actual $row.Installed
    }
    if (![IO.Directory]::Exists((Child $active 'installed'))) { Folder (Child $active 'installed') }
}
# Retain the first runtime receipt and each later Windows/console journal.
function Update-Applications {
    $dir = Child $active 'apps-2.2.0'
    $marker = $updatePaths[-1]
    $before = HashOf (Child $SdRoot $marker)
    if ($before -ceq $files[$marker] -and ![IO.Directory]::Exists($dir)) { return }
    if (!(Previous-Asset $marker $before) -and $before -cne $files[$marker]) { throw 'The installed edition does not match this update.' }
    if (![IO.Directory]::Exists($dir)) {
        $temp = Child $state ('stage-' + [guid]::NewGuid().ToString('N'))
        Folder $temp
        [IO.File]::WriteAllText((Child $temp 'stage.owner'),'KEFIRIZ-ATM-PREP-1')
        $lines = @('KEFIRJS-JOURNAL-1')
        for ($i=0; $i -lt $updatePaths.Count; $i++) {
            $name = $updatePaths[$i]
            $target = Child $SdRoot $name
            $old = HashOf $target
            $next = $files[$name]
            if (!$old -and (Missing-ServiceAllowed $name)) {
                # '-' is also the native updater's receipt for an absent original.
                $old = '-'
            } elseif ($old -cne $next) {
                if (!(Previous-Asset $name $old)) { throw "Unexpected installed application, preserved: $name" }
                CopyChecked $target (Child $temp ('old-{0:D2}.bin' -f $i)) $old
            }
            $lines += ("{0}`t{1}`t{2}`t{3}" -f $i,$name,$old,$next)
        }
        [IO.File]::WriteAllLines((Child $temp 'originals.tsv'),[string[]]$lines)
        MoveDirectory $temp $dir
    }
    $journal = Child $dir 'originals.tsv'
    if (![IO.File]::Exists($journal) -or ([IO.FileInfo]$journal).Length -gt 2048) { throw 'Missing application update journal.' }
    $lines = [IO.File]::ReadAllLines($journal)
    if ($lines.Count -ne ($updatePaths.Count + 1) -or $lines[0] -cne 'KEFIRJS-JOURNAL-1') { throw 'Invalid application update journal.' }
    $originals = @{}
    for ($i=0; $i -lt $updatePaths.Count; $i++) {
        $name = $updatePaths[$i]
        $columns = $lines[$i+1].Split([char]9)
        if ($columns.Count -ne 4 -or $columns[0] -cne [string]$i -or $columns[1] -cne $name -or
            $columns[3] -cne $files[$name]) {
            throw 'Invalid application entry in the card journal.'
        }
        $old = $columns[2]
        if ($old -ceq '-' -and (Missing-ServiceAllowed $name)) {
            $originals[$name] = ''
        } else {
            if ($old -cne $columns[3] -and !(Previous-Asset $name $old)) { throw 'Invalid original in application update journal.' }
            if ($old -cne $columns[3] -and (HashOf (Child $dir ('old-{0:D2}.bin' -f $i))) -cne $old) {
                throw "Application rollback copy is missing: $name"
            }
            $originals[$name] = $old
        }
        $live = HashOf (Child $SdRoot $name)
        if ($live -cne $columns[3] -and $live -cne $originals[$name]) { throw "The card changed during the application update: $name" }
    }
    for ($i=0; $i -lt $updatePaths.Count; $i++) {
        $name = $updatePaths[$i]
        $next = $files[$name]
        $target = Child $SdRoot $name
        $old = HashOf $target
        if ($old -ceq $next) { continue }
        if ($old -cne $originals[$name]) { throw "The card changed during the application update: $name" }
        $tmp = Child $dir ('new-' + [guid]::NewGuid().ToString('N'))
        CopyChecked (Child $bundle ('sd/' + $name)) $tmp $next
        Commit $tmp $target $old $next
    }
}
function Update-ThemesRetirement([bool]$restore) {
    # Fresh installs record removals in receipt.json; earlier installations
    # already have that immutable receipt and need their own rollback copy.
    $target = Child $SdRoot $themesPath
    $parked = Child $active 'retired-nxthemes-2.1.0.nro'
    $saved = HashOf $parked
    if ($saved -and $saved -cne $themesHash) { throw 'The NXThemes rollback copy has changed.' }
    $live = HashOf $target
    if ($restore) {
        if ($saved -and !$live) {
            Folder ([IO.Path]::GetDirectoryName($target))
            [IO.File]::Move($parked,$target)
        }
    } elseif ($live -ceq $themesHash) {
        if ($saved) { [IO.File]::Delete($target) }
        else { [IO.File]::Move($target,$parked) }
    }
}
function Restore-Applications {
    . (Join-Path $PSScriptRoot 'NativeJournal.ps1')
    $versionMarker = $updatePaths[-1]; $retiredNames = @($retired.Keys)
    $ota = Child $SdRoot 'config/kefirjs/atmosphere-ota'
    $interrupted = Child $ota 'active'
    if ([IO.Directory]::Exists($interrupted)) { Restore-ConsoleJournal $interrupted $true }
    foreach ($journal in @((Child $ota '2.2.0'),(Child $active 'apps-2.2.0'))) {
        if ([IO.Directory]::Exists($journal)) { Restore-ConsoleJournal $journal $false }
    }
    # A legacy console client writes the bridge first. Windows can finish it
    # before the updated client records its own same-version completion.
    foreach ($journal in @((Child $ota '2.1.1-complete'),(Child $active 'apps-2.1.1'),(Child $ota '2.1.1'))) {
        if ([IO.Directory]::Exists($journal)) { Restore-ConsoleJournal $journal $false }
    }
    foreach ($version in @('2.1.0','2.0.3','2.0.2','2.0.1')) {
        foreach ($journal in @((Child $ota $version),(Child $active ('apps-' + $version)))) {
            if ([IO.Directory]::Exists($journal)) { Restore-ConsoleJournal $journal $false }
        }
    }
}
function Protect-OldServices {
    $profiles = HashOf (Child $SdRoot $updatePaths[3])
    $transfers = HashOf (Child $SdRoot $updatePaths[4])
    if ($profiles -cin @($previousApps[$updatePaths[3]],'b0e562b0104617a2b743adb64deec7de2d6122a76afd5efd06d1b26635a74c87') -or
        $transfers -cin @($previousApps[$updatePaths[4]],'e1e6824a79658aa924dfa94b5099734923f1d3ebf5d3f45ec7af952d8d3af4bc')) {
        [void](Disable-KefirizProfileAutostart -CardRoot $SdRoot)
    }
}
function Restore-Prepared($r) {
    $previous = Child $active 'previous-atmosphere'
    $departing = Child $active 'restored-atmosphere-edition'
    if ($r.HadAtmosphere -and ![IO.Directory]::Exists($previous)) {
        $neverSwitched = [IO.Directory]::Exists((Child $active 'new-atmosphere')) -and [IO.Directory]::Exists($atm)
        $alreadyRestored = [IO.Directory]::Exists($departing) -and [IO.Directory]::Exists($atm)
        if (!$neverSwitched -and !$alreadyRestored) { throw 'Previous Atmosphere directory is missing. The backup was preserved.' }
    }
    Folder (Child $active 'restoring')
    for ($i=$r.Rows.Count-1; $i -ge 0; $i--) {
        $row = $r.Rows[$i]
        $target = Child $SdRoot $row.Path
        $live = HashOf $target
        if ($live -ceq $row.Original) { continue }
        if ($live -cne $row.Installed -and !($previousApps.ContainsKey($row.Path) -and
            ($live -ceq $files[$row.Path] -or (Previous-Asset $row.Path $live)))) { Write-Host "Preserved a later change: $($row.Path)"; continue }
        if ($row.Original) {
            $copy = Child $active ('restore-' + [guid]::NewGuid().ToString('N'))
            CopyChecked (Child $active "original-$i") $copy $row.Original
            Commit $copy $target $live $row.Original
        } else {
            [IO.File]::Move($target,(Child $active "removed-$i"))
        }
    }
    # Both directory renames are on this volume. Their presence records which step completed.
    if (([IO.Directory]::Exists($previous) -or !$r.HadAtmosphere) -and [IO.Directory]::Exists($atm)) {
        if ([IO.Directory]::Exists($departing)) { throw 'A new atmosphere directory appeared during restore.' }
        MoveDirectory $atm $departing
    }
    if ([IO.Directory]::Exists($previous)) { MoveDirectory $previous $atm }
    $archive = Child $state ('restored-' + [guid]::NewGuid().ToString('N'))
    MoveDirectory $active $archive
    Write-Host "Previous installation restored. Backup: $archive"
}
try {
    if ($Action -eq 'Download') {
        $name = 'KEFIRIZ-Meridian-2.2.0.zip'
        $zip = Join-Path $project $name
        if (!(Test-Path -LiteralPath $zip -PathType Leaf)) { $zip = Join-Path (Split-Path $project) $name }
        $local = Test-Path -LiteralPath $zip -PathType Leaf
        if ($SdRoot -or !$local) {
            $target = if ($SdRoot) { $SdRoot } else { $project }
            Folder $target
            $dest = Child (Get-Item -LiteralPath $target).FullName $name
            if ($local) {
                if (![IO.File]::Exists($dest)) { CopyChecked $zip $dest (HashOf $zip) }
                if ((HashOf $dest) -cne (HashOf $zip)) { throw 'An unrelated ZIP already exists at the download destination.' }
            } else { Get-PublishedArchive $dest $name }
            $zip = $dest
        }
        Write-Host "Offline installer: $zip"
        exit 0
    }
    if (!$artworkOnly -and $Action -ne 'Recover') {
        $files = Read-Bundle
        $table = [IO.File]::ReadAllText((Join-Path $tools 'retired-updater.json')) | ConvertFrom-Json
        $retired = @{}
        foreach ($item in $table.PSObject.Properties) {
            if ($item.Name -cnotmatch '^(bootloader/(ini|res)|config/kefir-updater|switch/kefir-updater)/[a-zA-Z0-9_.!-]+$' -or $item.Name -match '/\.{1,2}$') { throw 'Invalid legacy retirement table.' }
            $retired[$item.Name] = $item.Value
        }
        $retired[$themesPath] = @($themesHash)
    }
    if ($Action -in @('Install','Artwork','Undo-Artwork','Restore','Verify')) { Test-ArtworkTools }
    if ($Action -eq 'Verify') { Write-Host "Verified $($files.Count) distribution files and the upstream Atmosphere core."; exit 0 }
    if (!$SdRoot) {
        $letter=(Read-Host 'Enter the drive letter of your Horizon 20.5.0 Switch microSD').Trim().TrimEnd(':')
        if ($letter -notmatch '^[A-Za-z]$') { throw 'Enter one microSD drive letter.' }
        $SdRoot = $letter + ':\'
    }
    $card = Get-Item -LiteralPath $SdRoot -Force
    if ($card -isnot [IO.DirectoryInfo]) { throw 'Select the root of your Switch card.' }
    for ($dir=$card;$dir;$dir=$dir.Parent) {
        if ($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'A linked card path is unsupported.' }
    }
    $SdRoot=$card.FullName
    $same = [StringComparison]::OrdinalIgnoreCase
    if ($SdRoot.TrimEnd('\').Equals($project.TrimEnd('\'),$same) -or
        ($project.TrimEnd('\')+'\').StartsWith($SdRoot.TrimEnd('\')+'\',$same) -or
        ($SdRoot.TrimEnd('\')+'\').StartsWith($project.TrimEnd('\')+'\',$same)) { throw 'Choose the microSD, separate from the installer folder.' }
    $state = Child $SdRoot 'config/kefiriz/standalone-install'
    $active = Child $state 'active'
    $atm = Child $SdRoot 'atmosphere'
    if ($Action -eq 'Restore' -and ![IO.Directory]::Exists($active)) {
        $legacy = Join-Path $tools 'Get-Release.ps1'
        if ((Test-Path -LiteralPath $legacy -PathType Leaf) -and (@('config/kefiriz/distribution-installer/active','config/kefiriz/distribution-installer/release-1.1.0/active','config/kefiriz/distribution-installer/release-1.2.0/active','config/kefirjs/ota') | Where-Object { [IO.Directory]::Exists((Child $SdRoot $_)) } | Select-Object -First 1)) {
            & $legacy -Action Restore -SdRoot $SdRoot
            exit $LASTEXITCODE
        }
        throw 'No Atmosphere edition installation was found on this card. Use the original installer for a legacy backup.'
    }
    if ($Action -eq 'Recover') { Recover-Layouts; exit 0 }
    if (!(Test-Path -LiteralPath (Child $SdRoot 'bootloader/hekate_ipl.ini') -PathType Leaf) -and
        !(Test-Path -LiteralPath (Child $SdRoot 'bootloader/sys/nyx.bin') -PathType Leaf)) {
        throw 'Choose the card of an already configured Hekate Switch running Horizon 20.5.0.'
    }
    Folder $state
    $lock = [IO.File]::Open((Child $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
    try {
        $shared = Child $SdRoot 'config/kefiriz/distribution-installer/operation.lock'
        Folder ([IO.Path]::GetDirectoryName($shared))
        $legacyLock = [IO.File]::Open($shared,'OpenOrCreate','ReadWrite','None')
        try {
            if ($Action -in @('Install','Artwork','Undo-Artwork','Restore')) { . (Join-Path $tools 'NativeTheme.ps1') }
            if ($Action -eq 'Install' -and ![IO.Directory]::Exists($active) -and [IO.File]::Exists((Child $atm 'package3'))) {
                Recover-Layouts -OnlyIfActive -DistributionLock $legacyLock
            }
            if (!$artworkOnly) {
                . (Join-Path $PSScriptRoot 'DBI.ps1')
                . (Join-Path $PSScriptRoot 'ProfileRecovery.ps1')
            }
            if ($Action -eq 'Services') {
                . (Join-Path $PSScriptRoot 'Services.ps1')
                Enable-KefirizAuroraServices -SdRoot $SdRoot -BundleRoot $bundle -Files $files
                exit 0
            }
            if ($Action -eq 'Profiles') {
                if (!(Enable-KefirizProfiles -CardRoot $SdRoot -ExpectedSha256 $files[$updatePaths[3]])) { throw 'Install the repaired profile service first.' }
                exit 0
            }
            if ($Action -eq 'DBI') {
                if (!(Install-KefirizDbi -SdRoot $SdRoot -ProjectRoot $project -Download)) { throw 'DBI could not be installed.' }
                exit 0
            }
            if ($Action -in @('Artwork','Undo-Artwork')) {
                if (!(Test-Path -LiteralPath (Child $active 'installed') -PathType Container) -or
                    (HashOf (Child $atm 'package3')) -cnotin @($runtime,$KefirizMeridianCore)) { throw 'Install and start KEFIRIZ Atmosphere edition first.' }
                if ([IO.Directory]::Exists((Child $active 'restoring'))) { throw 'A distribution restore is in progress.' }
                if ($Action -eq 'Undo-Artwork') {
                    Undo-KefirizNativeRevision $SdRoot (Child $active 'native-artwork')
                    exit 0
                }
                if (!(Invoke-KefirizNativeTheme -Action Install -SdRoot $SdRoot -PackRoot $tools -Standalone -RetryAfterRecovery)) { exit 2 }
                exit 0
            }
            if ($Action -eq 'Install') { Protect-OldServices; Clear-AbandonedStages }
            if (![IO.Directory]::Exists($active)) {
                New-Stage
            }
            $receipt = Open-Receipt
            if ($Action -eq 'Restore') {
                Folder (Child $active 'restoring')
                [void](Restore-KefirizMeridianCore $SdRoot $active $bundle)
                Update-KefirizBootArtwork -Card $SdRoot -InstallState $active -Restore
                [void](Invoke-KefirizNativeTheme -Action Restore -SdRoot $SdRoot -PackRoot $tools -Standalone)
                Update-ThemesRetirement $true
                Restore-Applications
                Restore-Prepared $receipt
                Protect-OldServices
                exit 0
            }
            if ([IO.Directory]::Exists((Child $active 'restoring'))) { throw 'A restore was interrupted. Run install.bat restore.' }
            if (![IO.Directory]::Exists((Child $active 'installed'))) { Install-Prepared $receipt }
            elseif ((HashOf (Child $atm 'package3')) -cnotin @($runtime,$KefirizMeridianCore)) { throw 'The installed core has changed. The existing card was preserved.' }
            if ([IO.Directory]::Exists((Child $SdRoot 'config/kefirjs/atmosphere-ota/active'))) {
                throw 'An on-console update is interrupted. Reopen KEFIRIZ Updater to resume before updating these files.'
            }
            Protect-OldServices
            Update-Applications
            Update-ThemesRetirement $false
            [void](Restore-KefirizRecoveredProfile -CardRoot $SdRoot -ExpectedSha256 $files[$updatePaths[3]])
            [void](Enable-KefirizRecoveredTransfer -CardRoot $SdRoot -ExpectedSha256 $files[$updatePaths[4]])
            [void](Invoke-KefirizNativeTheme -Action Install -SdRoot $SdRoot -PackRoot $tools -Standalone -Automatic)
            try { [void](Install-KefirizDbi -SdRoot $SdRoot -ProjectRoot $project -Download) }
            catch { Write-Warning ("DBI setup: " + $_.Exception.Message + " Run install.bat dbi to retry.") }
            Update-KefirizBootArtwork -Card $SdRoot -InstallState $active
            [void](Install-KefirizMeridianCore $SdRoot $active $bundle)
            Write-Host 'KEFIRIZ Meridian 2.2.0 is installed. Safely eject the SD and start your usual KEFIRIZ entry in Hekate.'
            Write-Host 'Hold R while launching an installed game to open Homebrew and the Meridian guide.'
        } finally { $legacyLock.Dispose() }
    } finally { $lock.Dispose() }
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
exit 0
