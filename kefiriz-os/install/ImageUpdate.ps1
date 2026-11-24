# The published 0.2.0 card layout changes just these two files. Profile data
# and all Hekate/Meridian files live outside this transaction.
function ImageUpdateRows($manifest) {
    $prior = $manifest.upgradeFrom
    $paths = @('lakka/SYSTEM','lakka/SYSTEM.md5')
    if ($manifest.version -cne '0.2.0' -or $prior.version -cne '0.1.0' -or
        $prior.manifest -cnotmatch '^[0-9a-f]{64}$' -or $prior.manifest -ceq $pin -or
        $prior.files.Count -ne $paths.Count) { throw 'Invalid image update metadata.' }
    for ($i=0; $i -lt $paths.Count; $i++) {
        $path = $paths[$i]
        $old = $prior.files[$i]
        $target = @($manifest.files | Where-Object { $_.path -ceq $path })
        if ($target.Count -ne 1 -or $old.path -cne $path -or
            $old.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            [long]$old.size -le 0 -or [long]$old.size -gt 4294967295) { throw "Invalid image update file: $path" }
        [pscustomobject]@{path=$path; index=$i; old=$old; target=$target[0]}
    }
}

function StageImageFile([string]$source, [string]$staged, [string]$expected) {
    if ([IO.File]::Exists($staged)) {
        if ((HashOf $staged) -ceq $expected) { return }
        # A failed copy stays private to this transaction; the live file
        # remains untouched until all input files have been verified.
        [IO.File]::Delete($staged)
    }
    CopyChecked $source $staged $expected
}

function CommitImageReceipt($receipt, [string]$desiredPin) {
    if ($receipt.manifest -ceq $desiredPin) { return }
    $dest = Child $active 'receipt.json'
    $before = HashOf $dest
    $current = ReadReceipt
    if ($current.id -cne $receipt.id -or $current.manifest -cne $receipt.manifest) {
        throw 'The active installation changed during the update.'
    }
    $temp = Child $active ('image-receipt-'+[guid]::NewGuid().ToString('N')+'.json')
    try {
        $current.manifest = $desiredPin
        WriteReceipt $temp $current
        Commit $temp $dest $before (HashOf $temp)
    } finally {
        if ([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) }
    }
}

function SwitchLinuxImage($receipt, $manifest, [switch]$Rollback) {
    if (!(OwnsLinux $linux $receipt.id) -or ![IO.Directory]::Exists((Child $active 'installed')) -or
        [IO.Directory]::Exists((Child $active 'restoring'))) {
        throw 'The update requires a completed, owned KEFIRIZ OS installation.'
    }
    $transaction = Child $active ('image-update-'+$pin)
    $ready = Child $transaction 'ready'
    $undo = Child $transaction 'rolling-back'
    $hasBackup = [IO.Directory]::Exists($ready)
    if (!$Rollback -and [IO.Directory]::Exists($undo)) { throw 'Resume the interrupted RollbackUpdate first.' }
    if ($Rollback -and !$hasBackup) {
        if ($receipt.manifest -ceq $manifest.upgradeFrom.manifest -and ![IO.Directory]::Exists($transaction)) {
            Write-Host 'The original Linux release is installed.'; return
        }
        if ($receipt.manifest -cne $manifest.upgradeFrom.manifest -or ![IO.Directory]::Exists($transaction)) {
            throw 'This installation has no saved previous Linux release.'
        }
    }

    # Verify every live input and all required original images before the
    # first replacement. An interrupted commit may leave either known hash.
    $rows = @(ImageUpdateRows $manifest)
    $required = [long]0
    foreach ($row in $rows) {
        $row | Add-Member -NotePropertyName dest -NotePropertyValue (Child $Card $row.path)
        $row | Add-Member -NotePropertyName backup -NotePropertyValue (Child $transaction ('old-'+$row.index))
        $row | Add-Member -NotePropertyName incoming -NotePropertyValue (Child $transaction ('new-'+$row.index))
        $row | Add-Member -NotePropertyName live -NotePropertyValue (HashOf $row.dest)
        if ($row.live -cne $row.old.sha256 -and (!$hasBackup -or $row.live -cne $row.target.sha256)) {
            throw "The installed image differs from the supported release: $($row.path)"
        }
        if ($hasBackup -and (!$Rollback -or $row.live -cne $row.old.sha256) -and
            (HashOf $row.backup) -cne $row.old.sha256) { throw "Original image backup failed verification: $($row.path)" }
        if (!$Rollback) {
            if (!$hasBackup -and (HashOf $row.backup) -cne $row.old.sha256) {
                $bytes = if ([IO.File]::Exists($row.backup)) { ([IO.FileInfo]$row.backup).Length } else { 0 }
                $required += [Math]::Max([long]0, ([long]$row.old.size-$bytes))
            }
            if ($row.live -cne $row.target.sha256 -and (HashOf $row.incoming) -cne $row.target.sha256) {
                $bytes = if ([IO.File]::Exists($row.incoming)) { ([IO.FileInfo]$row.incoming).Length } else { 0 }
                $required += [Math]::Max([long]0, ([long]$row.target.size-$bytes))
            }
        }
    }
    if (!$Rollback) {
        foreach ($item in $manifest.files) {
            if ($item.path -cmatch '^lakka/(KERNEL|nx-plat\.dtimg|boot/.+)$' -and
                (HashOf (Child $Card $item.path)) -cne $item.sha256) {
                throw "Board or boot file differs from this release: $($item.path)"
            }
        }
        $free = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($Card)).AvailableFreeSpace
        if ($free -lt $required+2MB) { throw 'Insufficient space to stage the Linux image and its rollback copy.' }
        Folder $transaction
        foreach ($row in $rows) {
            if (!$hasBackup) { StageImageFile $row.dest $row.backup $row.old.sha256 }
            if ($row.live -cne $row.target.sha256) {
                StageImageFile (Child $bundle $row.path) $row.incoming $row.target.sha256
            }
        }
        Folder $ready
        foreach ($row in $rows) {
            if ($row.live -cne $row.target.sha256) { Commit $row.incoming $row.dest $row.old.sha256 $row.target.sha256 }
        }
        CommitImageReceipt $receipt $pin
        Write-Host "Installed KEFIRIZ OS $($manifest.version). The original image is available with RollbackUpdate."
        return
    }
    Folder $undo
    foreach ($row in $rows) {
        if ($row.live -cne $row.old.sha256) {
            Commit $row.backup $row.dest $row.target.sha256 $row.old.sha256
        }
    }
    CommitImageReceipt $receipt $manifest.upgradeFrom.manifest
    $archive = Child $active ('rolled-back-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::Move($transaction,$archive)
    Write-Host 'Restored the original Linux image. The 0.1.0 installer recognizes this installation again.'
}
