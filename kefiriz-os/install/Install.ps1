param([ValidateSet('Install','Restore','Verify','Status')][string]$Action='Install',[string]$Card)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CardFiles.ps1')
. (Join-Path $PSScriptRoot 'Menu.ps1')
$configFiles = @('bootloader/hekate_ipl.ini','bootloader/nyx.ini')
$owner = '.kefiriz-dualboot-owner'
$bundle = Join-Path $PSScriptRoot 'payload'

function ReadPackage([switch]$VerifyFiles) {
    $manifestFile = Join-Path $PSScriptRoot 'manifest.json'
    $script:pin = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'manifest.sha256')).Trim()
    if ($pin -cnotmatch '^[0-9a-f]{64}$' -or (HashOf $manifestFile) -cne $pin) { throw 'Package manifest failed SHA-256 verification.' }
    $m=[IO.File]::ReadAllText($manifestFile) | ConvertFrom-Json
    if ($m.format -ne 1 -or $m.product -cne 'KEFIRIZ OS' -or !$m.files.Count -or $m.files.Count -gt 5000 -or
        (HashOf (Join-Path $PSScriptRoot 'boot-entry.ini')) -cne $m.entrySha256) { throw 'Invalid KEFIRIZ OS release manifest.' }
    $seen=@{}
    foreach ($row in $m.files) {
        $p = $row.path
        if ($p -cnotmatch '^lakka/[ -~]+$' -or $p -match '[\\:*?"<>|]|(^|/)(\.{1,2}|CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])([./]|$)|//|[. ](/|$)' -or
            $p -eq "lakka/$owner" -or $p -match '/$' -or $seen.ContainsKey($p) -or
            $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or [long]$row.size -lt 0 -or [long]$row.size -gt 4294967295) { throw "Invalid package path or size: $p" }
        $seen[$p]=$true
        if ($VerifyFiles) {
            $f=Child $bundle $p
            if (![IO.File]::Exists($f) -or ([IO.FileInfo]$f).Length -ne $row.size -or (HashOf $f) -cne $row.sha256) { throw "Package file failed verification: $p" }
        }
    }
    foreach ($p in @('KERNEL','SYSTEM','nx-plat.dtimg','boot/bl31.bin','boot/bl33.bin','boot/boot.scr','boot/kefiriz-logo.bmp','boot/kefiriz-icon.bmp')) {
        if (!$seen.ContainsKey('lakka/'+$p)) { throw "The package is missing $p." }
    }
    $script:entry = MenuText (Join-Path $PSScriptRoot 'boot-entry.ini')
    $lines=@($entry.TrimEnd([char]13,[char]10) -split '\r?\n')
    $expected=@('[KEFIRIZ OS]','l4t=1','boot_prefixes=lakka/boot/','logopath=lakka/boot/kefiriz-logo.bmp','id=SWR-LAK','icon=lakka/boot/kefiriz-icon.bmp','bootargs_extra=nosplash')
    if ($lines.Count -ne $expected.Count -or [string]::Join("`n",$lines) -cne [string]::Join("`n",$expected)) { throw 'Unknown Linux boot entry.' }
    return $m
}
function SetCard {
    if (!$Card) {
        $drive = (Read-Host 'Drive letter of your configured KEFIRIZ microSD').Trim().TrimEnd('\',':')
        if ($drive -notmatch '^[a-z]$') { throw 'Enter one drive letter.' }
        $script:Card = $drive+':\'
    }
    if ($Card -match '^[A-Za-z]:?\\?$') { $script:Card = $Card.Substring(0,1)+':\' }
    $info = Get-Item -LiteralPath $Card -Force
    if ($info -isnot [IO.DirectoryInfo]) { throw 'Select the root directory of the microSD.' }
    for ($d=$info;$d;$d=$d.Parent) {
        if ($d.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked card paths are unsupported.' }
    }
    $script:Card=$info.FullName
    $cardPrefix=$Card.TrimEnd('\')+'\'
    $toolPrefix=$PSScriptRoot.TrimEnd('\')+'\'
    if ($toolPrefix.StartsWith($cardPrefix,[StringComparison]::OrdinalIgnoreCase) -or $cardPrefix.StartsWith($toolPrefix,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Extract the installer on the computer and select a separate microSD.'
    }
    if ($Action -eq 'Install') {
        $required=@('bootloader/hekate_ipl.ini','bootloader/sys/nyx.bin','atmosphere/package3')
        $required+=@('bpmpfw.bin','bpmpfw_b01.bin','mtc_tbl_b01.bin','sc7entry.bin','sc7exit.bin','sc7exit_b01.bin' | ForEach-Object { 'bootloader/sys/l4t/'+$_ })
        foreach ($p in $required) {
            $file=Child $Card $p
            if (![IO.File]::Exists($file) -or ([IO.FileInfo]$file).Length -eq 0) { throw "Missing Switch boot file: $p" }
        }
    }
    $script:state=Child $Card 'config/kefiriz-os/dual-boot'
    $script:active=Child $state 'active'
    $script:linux=Child $Card 'lakka'
    if ($info.FullName.TrimEnd('\') -ceq $info.Root.FullName.TrimEnd('\')) {
        $volume=[IO.DriveInfo]::new($info.Root.FullName)
        if ($volume.DriveFormat -ne 'FAT32') { throw 'Select the FAT32 volume of the Hekate microSD.' }
    }
}
function MakeReceipt {
    if (Test-Path -LiteralPath $linux) { throw 'A separate lakka directory or file already exists. Its contents were preserved.' }
    $mainText=MenuText (Child $Card $configFiles[0])
    $nyxText=MenuText (Child $Card $configFiles[1])
    [void](NumericConfig $nyxText 'homescreen')
    $desired=@((PrepareMain $mainText $entry $Card),(PutConfig $nyxText 'homescreen' '2'))
    $id=[guid]::NewGuid().ToString('N')
    $temp=Child $state ('prepare-'+$id)
    Folder $temp
    $rows=@(for ($i=0;$i -lt 2;$i++) {
        $dest=Child $Card $configFiles[$i]
        $before=HashOf $dest
        if ($before) { CopyChecked $dest (Child $temp "$i.old") $before }
        if ((MenuText $dest) -cne @($mainText,$nyxText)[$i]) { throw 'Boot configuration changed during preparation.' }
        PutMenu (Child $temp "$i.new") $desired[$i]
        [pscustomobject]@{before=$before;after=(HashOf (Child $temp "$i.new"))}
    })
    WriteReceipt (Child $temp 'receipt.json') ([pscustomobject]@{format=1;id=$id;manifest=$pin;files=$rows})
    [IO.Directory]::Move($temp,$active)
}
function ReadReceipt {
    $r=[IO.File]::ReadAllText((Child $active 'receipt.json')) | ConvertFrom-Json
    if ($r.format -ne 1 -or $r.id -cnotmatch '^[0-9a-f]{32}$' -or $r.manifest -cne $pin -or $r.files.Count -ne 2) {
        throw 'This installation belongs to a different package or has an invalid receipt.'
    }
    for ($i=0;$i -lt 2;$i++) {
        if ($r.files[$i].before -isnot [string] -or $r.files[$i].after -cnotmatch '^[0-9a-f]{64}$' -or
            (HashOf (Child $active "$i.old")) -cne $r.files[$i].before -or
            (HashOf (Child $active "$i.new")) -cne $r.files[$i].after) { throw 'Boot menu rollback copies failed verification.' }
    }
    return $r
}
function ConfigCommit([int]$index,[string]$src,[string]$before,[string]$after) {
    $dest=Child $Card $configFiles[$index]
    $live=HashOf $dest
    if ($live -ceq $after) { return }
    if ($live -cne $before) { throw "A boot configuration changed; preserved $($configFiles[$index])." }
    if (!$after) { [IO.File]::Move($dest,(Child $active ('removed-'+[guid]::NewGuid().ToString('N')+'.ini'))); return }
    $stage=Child $active ('commit-'+[guid]::NewGuid().ToString('N')+'.ini')
    try { CopyChecked $src $stage $after; Commit $stage $dest $before $after }
    finally { if ([IO.File]::Exists($stage)) { [IO.File]::Delete($stage) } }
}
function OwnsLinux([string]$dir,[string]$id) {
    $marker=Child $dir $owner
    return ([IO.File]::Exists($marker) -and ([IO.FileInfo]$marker).Length -eq 32 -and [IO.File]::ReadAllText($marker) -ceq $id)
}
function InstallLinux($r,$m) {
    if ([IO.Directory]::Exists((Child $active 'restoring'))) { throw 'A restore was interrupted. Run INSTALL.bat restore.' }
    if (Test-Path -LiteralPath $linux) {
        if (!(OwnsLinux $linux $r.id)) { throw 'An unowned lakka installation occupies this card.' }
        $already=[IO.Directory]::Exists((Child $active 'installed'))
        foreach ($row in $m.files) {
            if ($already -and $row.path -cnotmatch '^lakka/(KERNEL|SYSTEM|nx-plat\.dtimg|boot/.+)$') { continue }
            if ((HashOf (Child $Card $row.path)) -cne $row.sha256) { throw "A Linux file no longer matches this package and was preserved: $($row.path)" }
        }
    } else {
        $stage=Child $active 'staging'
        $free=[IO.DriveInfo]::new([IO.Path]::GetPathRoot($Card)).AvailableFreeSpace
        $required=[long]0
        foreach ($row in $m.files) {
            $dst=Child $stage $row.path
            $existing=if ([IO.File]::Exists($dst)) { ([IO.FileInfo]$dst).Length } else { 0 }
            $required += [Math]::Max([long]0,([long]$row.size-$existing))
        }
        if ($free -lt ($required+2MB)) { throw 'Insufficient free space on the selected volume to stage KEFIRIZ OS.' }
        Folder (Child $stage 'lakka/storage')
        foreach ($row in $m.files) {
            $dst=Child $stage $row.path
            if ([IO.File]::Exists($dst)) {
                if ((HashOf $dst) -ceq $row.sha256) { continue }
                # Only an uncommitted file inside this receipt's private staging tree.
                [IO.File]::Delete($dst)
            }
            if ($row.size -gt 64MB) { Write-Host ("Copying and verifying {0} ({1:N0} MiB) on the microSD..." -f $row.path,($row.size/1MB)) }
            CopyChecked (Child $bundle $row.path) $dst $row.sha256
        }
        $prepared=Child $stage 'lakka'
        [IO.File]::WriteAllText((Child $prepared $owner),$r.id)
        [IO.Directory]::Move($prepared,$linux)
    }
    if ([IO.Directory]::Exists((Child $active 'installed'))) {
        if (!(Section (MenuText (Child $Card $configFiles[0])) 'KEFIRIZ OS' -Optional)) { throw 'The installed menu was changed. Its current configuration was preserved.' }
        Write-Host 'KEFIRIZ OS is already installed.'; return
    }
    # Validate both inputs before publishing either. Disable autoboot last.
    for ($i=0;$i -lt 2;$i++) {
        if ((HashOf (Child $Card $configFiles[$i])) -cnotin @($r.files[$i].before,$r.files[$i].after)) { throw 'Boot menu changed since preparation; no configuration was overwritten.' }
    }
    foreach ($i in 1,0) { ConfigCommit $i (Child $active "$i.new") $r.files[$i].before $r.files[$i].after }
    Folder (Child $active 'installed')
    Write-Host 'KEFIRIZ OS is installed. Hekate opens the Launch menu with KEFIRIZ Meridian and KEFIRIZ OS.'
}
function RestoreLinux($r) {
    $backup=Child $active 'saved-lakka'
    if ((Test-Path -LiteralPath $linux) -and !(OwnsLinux $linux $r.id)) { throw 'The active lakka directory is not owned by this installation.' }
    $restore=Child $active 'restoring'
    if (![IO.Directory]::Exists($restore)) {
        $temp=Child $active ('restore-'+[guid]::NewGuid().ToString('N'))
        Folder $temp
        $rows=@(for ($i=0;$i -lt 2;$i++) {
            $dest=Child $Card $configFiles[$i]
            $live=HashOf $dest
            $orig=MenuText (Child $active "$i.old")
            $value=MergeMenuRestore (MenuText $dest) $orig (MenuText (Child $active "$i.new")) $entry ($i -eq 0)
            if ($live -cne (HashOf $dest)) { throw 'Boot configuration changed during restore preparation.' }
            $after=''
            if ($value -or $r.files[$i].before) { PutMenu (Child $temp "$i.desired") $value; $after=HashOf (Child $temp "$i.desired") }
            [pscustomobject]@{before=$live;after=$after}
        })
        WriteReceipt (Child $temp 'plan.json') ([pscustomobject]@{files=$rows})
        [IO.Directory]::Move($temp,$restore)
    }
    $plan=[IO.File]::ReadAllText((Child $restore 'plan.json')) | ConvertFrom-Json
    if ($plan.files.Count -ne 2) { throw 'Invalid restoration record.' }
    for ($i=0;$i -lt 2;$i++) {
        $item=$plan.files[$i]
        $src=Child $restore "$i.desired"
        if ($item.before -isnot [string] -or $item.after -isnot [string] -or (HashOf $src) -cne $item.after) { throw 'Invalid restored menu data.' }
        ConfigCommit $i $src $item.before $item.after
    }
    if ([IO.Directory]::Exists($linux)) { [IO.Directory]::Move($linux,$backup) }
    $archive=Child $state ('restored-'+$r.id)
    [IO.Directory]::Move($active,$archive)
    Write-Host "Previous boot menu restored. Linux files and saves are in $archive"
}
try {
    $manifest = ReadPackage -VerifyFiles:($Action -in @('Install','Verify'))
    if ($Action -eq 'Verify') { Write-Host "Verified $($manifest.files.Count) KEFIRIZ OS files."; exit 0 }
    SetCard
    if ($Action -eq 'Status') {
        if (![IO.Directory]::Exists($active)) { Write-Host 'KEFIRIZ OS has no active installation receipt on this card.'; exit 0 }
        $r=ReadReceipt
        if ((OwnsLinux $linux $r.id) -and [IO.Directory]::Exists((Child $active 'installed')) -and ![IO.Directory]::Exists((Child $active 'restoring')) -and
            [IO.File]::Exists((Child $linux 'SYSTEM')) -and (Section (MenuText (Child $Card $configFiles[0])) 'KEFIRIZ OS' -Optional)) { Write-Host 'KEFIRIZ OS and its boot menu are installed.' }
        else { Write-Host 'An installation or restoration is incomplete.' }
        exit 0
    }
    if ($Action -eq 'Restore' -and ![IO.Directory]::Exists($active)) { Write-Host 'No KEFIRIZ OS installation to restore.'; exit 0 }
    Folder $state
    $locks=@()
    try {
        $locks+=[IO.File]::Open((Child $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
        $meridianLock=Child $Card 'config/kefiriz/standalone-install/operation.lock'
        if ([IO.File]::Exists($meridianLock)) { $locks+=[IO.File]::Open($meridianLock,'Open','ReadWrite','None') }
        if (![IO.Directory]::Exists($active)) { MakeReceipt }
        $r=ReadReceipt
        if ($Action -eq 'Restore') { RestoreLinux $r } else { InstallLinux $r $manifest }
    } finally { foreach ($lock in $locks) { $lock.Dispose() } }
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
exit 0
