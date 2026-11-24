# KEFIRIZ OS dual boot

This Windows installer adds KEFIRIZ OS alongside the existing KEFIRIZ Meridian games installation on a Switch microSD. KEFIRIZ OS uses the Switchroot Lakka Linux boot stack and its own graphical session. The installation occupies the FAT32 volume. Allow at least 2 GB of free space. [Switchroot FAT32 installation guide](https://wiki.switchroot.org/wiki/linux/l4t-lakka-install-update-guide)

## Install

1. Extract the whole package to your PC. Connect the existing Hekate microSD and inspect its drive letter in File Explorer. Existing KEFIRIZ and Hekate boot files must already be present. Switchroot specifies Hekate 6.0.6 or newer.
2. Run **INSTALL.bat**, enter the microSD drive letter and wait for the installation to complete. For example, the command-line equivalent is `INSTALL.bat install E`.
3. Safely eject the card and restart the Switch. Hekate opens its Launch menu. **KEFIRIZ Meridian** starts the existing games environment. **KEFIRIZ OS** starts Linux. The original Full Stock entry remains available on cards configured with it. [Hekate Launch and Nyx configuration](https://github.com/CTCaer/hekate#nyx-configuration-keysvalues-nyxini)

**INSTALL.bat verify** checks the files on the PC. **INSTALL.bat status E** reports the installation state of the selected card. After an interrupted install, run the installation again with the same package and card. An existing independent `lakka` directory is preserved and reported before installation.

## Restore

Run **INSTALL.bat restore E** from the same extracted version. This restores the previous Hekate menu and autoboot setting. Linux files and saves are moved together into the backup path printed by the installer, under `config/kefiriz-os/dual-boot/restored-…/saved-lakka`. The original INI snapshots are in that same archived transaction directory. Re-running Restore resumes an interrupted restoration.

Menu changes use Hekate’s main `bootloader/hekate_ipl.ini` and Nyx’s `bootloader/nyx.ini`. The installer verifies the Linux system tree before making its menu entry visible. If you edit the Linux entry yourself, retain a copy of it. Restore preserves other menu edits and stops if it cannot safely identify the entry it installed.

## Build provenance

The package preparer verifies the original Switchroot Lakka archive and hardware boot files, then stages the customized SquashFS system and its matching checksum. The accompanying manifest records the SHA-256 and byte length of each packaged SD file. The original distribution’s license notices are under `payload/lakka/licenses`. [Upstream Switch image](https://nightly.builds.lakka.tv/members/gavin/lakka-v5.x-new/) · [Lakka source](https://github.com/libretro/Lakka-LibreELEC)
