# KEFIRIZ 2.0.0 · Atmosphere edition

This hardware-preview build targets a Switch already configured for Hekate and Horizon 20.5.0. The full installer uses official Atmosphère 1.10.1, an independent signed update channel, KEFIRIZ Homebrew, System, game profiles and background transfers. Moving from the 786 edition requires the full installer. Each channel retains its own release history.

## Recover a boot crash with a theme

Power the Switch off. Unzip the recovery download on Windows, connect the microSD and run **KEFIRIZ-Theme-Recovery.cmd**. It saves known active HOME and lock-screen layouts to a backup on the card. Safely eject the card and boot your usual CFW. If the crash continues, record the Program ID from the error screen and retain the newest Atmosphère crash report. [Theme recovery download](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.0.0/KEFIRIZ-Theme-Recovery.zip)

## Install the independent edition

1. Back up the microSD. Extract the full archive on a Windows PC, run **install.bat**, and select the existing Hekate card.
2. In **Hekate → More Configs**, select **KEFIRIZ (sysMMC)** if the original version ended in **/S**. **KEFIRIZ** follows the global emuMMC setting. Boot the same storage where your games are installed.
3. Check the first boot, the lock screen after sleep, and **System Settings → Themes → Basic White / Basic Black**.
4. For custom artwork inside Nintendo HOME, open **NXThemes Installer → Extract home menu → Extract szs layout files** on this console. Power off and run **install.bat artwork** with the microSD connected to the PC. The renderer verifies the export and lists each screen it installs. The lock screen uses its original layout throughout this release.

The installer saves the previous runtime on the card and can resume an interrupted operation. **install.bat restore** restores that runtime. A physical Switch boot and sleep/wake cycle still need verification. [Full installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.0.0/KEFIRIZ-Atmosphere-2.0.0.zip)
