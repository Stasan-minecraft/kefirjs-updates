# KEFIRIZ OS dual boot

This Windows installer adds KEFIRIZ OS alongside the existing KEFIRIZ Meridian games installation on a Switch microSD. KEFIRIZ OS uses the Switchroot Lakka Linux boot stack and its own graphical session. The installation occupies the FAT32 volume. Allow at least 2 GB of free space. [Switchroot FAT32 installation guide](https://wiki.switchroot.org/wiki/linux/l4t-lakka-install-update-guide)

## Install

1. Extract the whole package to your PC. Connect the existing Hekate microSD and inspect its drive letter in File Explorer. Existing KEFIRIZ and Hekate boot files must already be present. Switchroot specifies Hekate 6.0.6 or newer.
2. Run **INSTALL.bat**, enter the microSD drive letter and wait for the installation to complete. For example, the command-line equivalent is `INSTALL.bat install E`.
3. Safely eject the card and restart the Switch. Hekate opens its Launch menu. **KEFIRIZ Meridian** starts the existing games environment. **KEFIRIZ OS** starts Linux. The original Full Stock entry remains available on cards configured with it. [Hekate Launch and Nyx configuration](https://github.com/CTCaer/hekate#nyx-configuration-keysvalues-nyxini)

**INSTALL.bat verify** checks the files on the PC. **INSTALL.bat status E** reports the installation state of the selected card. After an interrupted install, run the installation again with the same package and card. An existing independent `lakka` directory is preserved and reported before installation.

## Boot diagnostics

Open **KEFIRIZ-BOOT.bat** and select **1** for a read-only check of the Hekate entry, board-support files and six release hashes on the microSD. The command-line equivalent in the full installer is `INSTALL.bat Diagnose E`.

The message **Board was not initialized properly! Hang prevented.** comes from Switchroot U-Boot during board setup on Mariko. Linux and the KEFIRIZ desktop have not started at this stage. A passing card check and this message together narrow the investigation to the earlier boot stage. A technician familiar with the console can check compatibility of its board and boot hardware with Switchroot. Meridian has a separate entry for installed Switch games. [Switchroot boot stages](https://wiki.switchroot.org/wiki/linux/linux-bootstack-documentation) · [Lakka hardware guidance](https://www.lakka.tv/doc/Nintendo-Switch/)

Select **2** to set the normal Linux restart destination to Hekate on an existing KEFIRIZ installation. In the full installer use `INSTALL.bat BootMenu E`. This applies after Linux is running. U-Boot independently controls the ten-second restart shown on the board error screen.

After updating an existing boot entry, use this revision of the installer for rollback. The compact kit also supports `powershell -NoProfile -ExecutionPolicy Bypass -File .\Install.ps1 -Action Restore -Card E` when run from its extracted folder.

## Restore the previous configuration

Run **INSTALL.bat restore E** from the same extracted version. This restores the previous Hekate menu and autoboot setting. Linux files and saves are moved together into the backup path printed by the installer, under `config/kefiriz-os/dual-boot/restored-…/saved-lakka`. The original INI snapshots are in that same archived transaction directory. Re-running Restore resumes an interrupted restoration.

Menu changes use Hekate’s main `bootloader/hekate_ipl.ini` and Nyx’s `bootloader/nyx.ini`. The installer verifies the Linux system tree before making its menu entry visible. If you edit the Linux entry yourself, retain a copy of it. Restore preserves other menu edits and stops if it cannot safely identify the entry it installed.

## Build provenance

The package preparer verifies the original Switchroot Lakka archive and hardware boot files, then stages the customized SquashFS system and its matching checksum. The accompanying manifest records the SHA-256 and byte length of each packaged SD file. The original distribution’s license notices are under `payload/lakka/licenses`. [Upstream Switch image](https://nightly.builds.lakka.tv/members/gavin/lakka-v5.x-new/) · [Lakka source](https://github.com/libretro/Lakka-LibreELEC)
