# KEFIRIZ Aurora 2.1.0

An independent KEFIRIZ release for Horizon 20.5.0 with Atmosphère 1.10.1. Aurora is published on **Preview** while hardware boot, controller input, the managed HOME layer and sleep/wake await verification on a physical Switch.

## Installation

- **Existing KEFIRIZ 2.0.x.** Check Stable for 2.0.3, install it and reopen **KEFIRIZ Updater**. Press **X** or tap **Switch channel** to select **Preview**. Choose **Review update**, then **Install update**. Reboot into your usual Hekate configuration.
- **Offline update from a phone.** Power off and back up the card, then extract the offline ZIP to the microSD root. Open **KEFIRIZ Updater**, select **Preview**, review and install. The archive includes the new updater and a signed local payload. [Offline Aurora update](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.1.0/KEFIRIZ-Aurora-2.1.0-Offline.zip)
- **Windows installation.** Extract the full archive on the PC and run **install.bat** with the microSD connected. For a previously used /S environment, choose **KEFIRIZ (sysMMC)** under More Configs in Hekate. [Full Windows installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.1.0/KEFIRIZ-Aurora-2.1.0.zip)

On an older 786 installation, the Windows installer migrates the card to the independent edition. The 786 client also receives the channel selector in its own 1.2.3 release. Its Preview channel currently follows the same version as Stable.

## The first boot

When the Profiles service reaches HOME, it displays a five-page Aurora guide. Press and release **R + ZR** to advance. Finishing the last page saves completion for this version. Reopen **KEFIRIZ Guide** from Homebrew Favorites at any time. Hold **R** while starting an installed game to open Homebrew. If Profiles was disabled, the guide also opens on the first Homebrew visit. From **KEFIRIZ Updater**, use **Y / Enable R + ZR** to enable the verified Aurora service and reboot.

## Changes

- **R + ZR** shows the active preset for five seconds. Release both buttons and press them together again to change the preset. HOME controls the global default. A game uses its own profile. The service registers handheld and external controllers and retries transient input initialization failures.
- **KEFIRIZ Updater** displays the selected feed and checks it when the selection changes. Each edition stores its own channel on the card. The updater verifies signatures and checks the installed version before applying an update.
- Favorites now show **KEFIRIZ Torrent**, **KEFIRIZ Updater**, **KEFIRIZ System**, **DBI**, and **KEFIRIZ Guide**. **X** opens All apps and **+** opens classic nx-hbmenu. The installer and native updater retire the exact NXThemes binary shipped by the previous releases. The rollback journal keeps its original bytes. Other theme files and independently installed versions are preserved.
- The background transfer service sleeps on a kernel port between requests. Saving a request wakes the worker. Its idle reconciliation interval goes from 2.5 to 15 seconds when the port is available. Network retries keep separate deadlines.
- Aurora's boot entry has a new icon. The new splash appears on Hekate autoboot with boot wait enabled. The later Atmosphère and Nintendo graphics are supplied by their respective boot stages.

Tests covered ARM64 builds, compiled service permissions, the controller state machine, actual software-rendered cards, signed upgrades, recovery and Windows installation on test directories. The official Atmosphère core is byte-identical to upstream. There is no measured console FPS or boot-time claim.
