function TestBootFiles($manifest) {
    $problems = [Collections.Generic.List[string]]::new()
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
        $p = Child $Card $row.path
        if (![IO.File]::Exists($p) -or ([IO.FileInfo]$p).Length -ne $row.size -or (HashOf $p) -cne $row.sha256) {
            $problems.Add("Missing or damaged release file: $($row.path)")
        } else {
            $verified++
        }
    }
    if ($problems.Count) { throw ($problems -join [Environment]::NewLine) }
    Write-Host "Verified $verified release files and the Hekate L4T entry on the card."
    Write-Host 'If U-Boot shows "Board was not initialized properly! Hang prevented.", early board setup stopped before Linux.'
    Write-Host 'On a modified console, have the board and its boot hardware checked for Switchroot compatibility. The SD files cannot identify that hardware.'
}
