# KEFIRIZ 1.2.2

Fixes the startup crash **2168-0008 (0x10a8)** in KEFIRIZ Profiles (42000000004B4650) and the matching issue in KEFIRIZ Transfers (42000000004B4654). A byte-for-byte rebuild of the original release matched the trace. Both programs lacked permissions their runtime needs before application initialization. We tested the old and repaired startup instructions in ARM64 emulation. Testing boot, Wi-Fi and real gameplay on a physical Switch remains outstanding.

## Recover and update

1. Power off the console. Back up the card, then extract the matching repair ZIP **to the root of the microSD**. Allow replacement of the two service binaries and the updater. Both boot-repair binaries live at their standard Atmosphère SD paths so the fix is present before HOME starts. [Download this edition's repair kit](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.0.2/KEFIRJS-Repair-786-1.2.2.zip)
2. Boot the same Horizon 20.5.0 installation as before. Open KEFIRJS Updater, select **Review update**, then **Install update**. Reboot to load the updated components and see the Hekate artwork on the KEFIRIZ boot entry.
3. Previously enabled profile and transfer services keep their startup settings. If you manually renamed a boot2.flag before this repair, restore its original filename after installing the repaired binaries to enable the service again.

## Homebrew

Favorites show **KEFIRIZ Torrent**, **KEFIRJS Updater**, **KEFIRIZ System**, **DBI** and **NXThemes Installer**, with original KEFIRIZ artwork. **X** opens All apps and **+** opens classic nx-hbmenu. The new menu embeds its favorite icons. The profile service polls foreground process information less often while keeping the R + ZR input interval.

KEFIRJS Updater waits for Horizon network access, verifies HTTPS on GitHub's raw-content and contents API endpoints and reports the failing network stage. A complete signed offline kit works with Wi-Fi off. Older working clients first receive a small application update and can reopen the updater to finish the service repair.
