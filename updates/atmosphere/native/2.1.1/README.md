# KEFIRIZ Aurora 2.1.1

Aurora 2.1.1 brings custom artwork into Nintendo HOME on Horizon 20.5.0. The release uses the official Atmosphère 1.10.1 core and is available on the Preview channel.

## Native HOME artwork

The renderer keeps Horizon's background layer, text colors and toolbar color controls. Aurora adds translucent mint and blue accents along the screen edges and replaces the nine native HOME toolbar icon images. Change **System Settings → Themes → Basic White / Basic Black** to switch the system palette. HOME keeps its regular buttons and actions. Settings, All Software, News and user screens receive accents when the corresponding original resources are available. The lock screen uses Nintendo's original layout.

1. Download the Windows installer, extract it on the PC, run **install.bat** and select the Switch microSD. With an existing compatible export, the installer generates and applies the artwork during installation. [Windows installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.1.1/KEFIRIZ-Aurora-2.1.1.zip)
2. If the installer reports missing resources, boot Aurora. Hold **R** while launching an installed game to open KEFIRIZ Homebrew. It requests the running HOME resources in the background. When the ready message appears, power off the console and run **install.bat** on the card again. **KEFIRIZ System** also shows the result or the error returned by Horizon. Press **A** in System to retry.
3. Reboot into the same Hekate configuration you normally use. If theme recovery was previously run on your card, the automatic installer respects that choice. After a fresh capture use **install.bat artwork** to explicitly retry. **KEFIRIZ-Theme-Recovery.cmd** can park active layouts from Windows if HOME fails to start.

## Console update

Open **KEFIRIZ Updater**, select **Preview**, review 2.1.1 and install. With an older online updater, reopen it on Preview after the first update. Install the remaining Profiles update, then restart. Stable stays on 2.0.3. For a one-pass offline update, extract the offline kit to the microSD root and open its included updater on Preview. The Windows installer generates the personalized HOME files from the console's own resources. [Offline console update](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.1.1/KEFIRIZ-Aurora-2.1.1-Offline.zip)

Desktop tests exercise packaging, signed updates, installation, recovery and synthetic layout resources. The renderer also processed six screens from a console's original 20.5.0 export. Across 2,321 input archive entries, all 1,784 animation files retained their original bytes. The nine toolbar icons kept their original system color settings. Boot, display, sleep/wake and on-device resource capture still need verification on hardware.
