function TestBootFiles($manifest) {
    $problems = [Collections.Generic.List[string]]::new()
    $expectedImage = $null
    if ($manifest.upgradeFrom -and [IO.Directory]::Exists($active)) {
        $receipt = ReadReceipt
        $update = Child $active ('image-update-'+$pin)
        if ([IO.Directory]::Exists((Child $update 'rolling-back'))) { throw 'Resume RollbackUpdate before diagnosing the restored image.' }
        if ($receipt.manifest -cne $pin) {
            if ([IO.Directory]::Exists($update)) { throw 'Resume Install or RollbackUpdate before diagnosing the image.' }
            $expectedImage = $manifest.upgradeFrom.files[0]
            Write-Host "Checking installed Linux release $($manifest.upgradeFrom.version)."
        }
    }
    $main = MenuText (Child $Card 'bootloader/hekate_ipl.ini')
    $linuxEntry = Section $main 'KEFIRIZ OS' -Optional
    $meridian = Section $main 'KEFIRIZ Meridian' -Optional

    if (!$linuxEntry) {
        $problems.Add('Hekate has no KEFIRIZ OS entry.')
    } else {
        foreach ($pair in @('l4t=1','boot_prefixes=lakka/boot/','id=SWR-LAK')) {
            $name, $value = $pair.Split('=')
            $setting = KeyMatch $linuxEntry.Body $name
            if (!$setting -or $setting.Groups[2].Value -cne $value) {
                $problems.Add("Check $name in the KEFIRIZ OS entry.")
            }
        }
        foreach ($key in @('payload','pkg3','fss0')) {
            if (KeyMatch $linuxEntry.Body $key) {
                $problems.Add("Conflicting Hekate boot option in the KEFIRIZ OS entry: $key")
            }
        }
        $restart = KeyMatch $linuxEntry.Body 'r2p_action'
        if ($restart) {
            Write-Host ('Linux restart target: ' + $restart.Groups[2].Value)
        } else {
            Write-Host 'Linux restart target: self (Switchroot default).'
        }
    }
    if ($meridian) { Write-Host 'Hekate also lists KEFIRIZ Meridian for Switch games.' }

    foreach ($name in @('bpmpfw.bin','bpmpfw_b01.bin','mtc_tbl_b01.bin','sc7entry.bin','sc7exit.bin','sc7exit_b01.bin')) {
        $relative = 'bootloader/sys/l4t/' + $name
        $p = Child $Card $relative
        if (![IO.File]::Exists($p) -or ([IO.FileInfo]$p).Length -eq 0) {
            $problems.Add("Missing or empty Hekate board-support file: $relative")
        }
    }
    Write-Host 'Reading the kernel, system image and boot files from the selected card...'
    $verified = 0
    foreach ($row in $manifest.files) {
        if ($row.path -cnotmatch '^lakka/(KERNEL|SYSTEM|nx-plat\.dtimg|boot/(bl31\.bin|bl33\.bin|boot\.scr))$') { continue }
        if ($expectedImage -and $row.path -ceq $expectedImage.path) { $row = $expectedImage }
        $p = Child $Card $row.path
        if (![IO.File]::Exists($p) -or ([IO.FileInfo]$p).Length -ne $row.size -or (HashOf $p) -cne $row.sha256) {
            $problems.Add("Missing or damaged release file: $($row.path)")
        } else {
            $verified++
        }
    }
    if ($problems.Count) { throw ($problems -join [Environment]::NewLine) }
    Write-Host "Verified $verified release files and the Hekate L4T entry on the card."
    if ($manifest.version -ceq '0.2.0' -and !$expectedImage) {
        $record = Child $Card 'lakka/storage/kefiriz/session-0.2.0.txt'
        if ([IO.File]::Exists($record) -and ([IO.FileInfo]$record).Length -le 128) {
            $content = [IO.File]::ReadAllText($record)
            if ($content -cmatch '\Aversion=0\.2\.0\r?\nboard=(nintendo,(?:icosa|iowa|hoag|aula))\r?\n?\z') {
                Write-Host "Saved desktop record: KEFIRIZ OS 0.2.0 previously displayed a frame on $($Matches[1])."
            } else {
                Write-Host 'The saved desktop record has an unrecognized format.'
            }
        } else {
            Write-Host 'No recognized KEFIRIZ OS 0.2.0 desktop record was found on this card.'
        }
    }
    Write-Host 'If U-Boot shows "Board was not initialized properly! Hang prevented.", early board setup stopped before Linux.'
    Write-Host 'On a modified console, have the board and its boot hardware checked for Switchroot compatibility. The SD files cannot identify that hardware.'
}
