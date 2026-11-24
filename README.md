# KEFIRIZ · Switch updates

**[KEFIRIZ 2.0.0 · independent Atmosphère edition (hardware preview)](updates/atmosphere/2.0.0/README.md)** uses upstream Atmosphère 1.10.1 on Horizon 20.5.0 and boots with the original Nintendo HOME and lock screen. The installer includes native artwork preparation and microSD recovery. [Download the full Windows installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.0.0/KEFIRIZ-Atmosphere-2.0.0.zip)

**Boot crash after applying a theme?** Power off the console, unzip the recovery tool on Windows and run **KEFIRIZ-Theme-Recovery.cmd** with the microSD connected. It backs up the active theme files before the next boot. [Download theme recovery](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.0.0/KEFIRIZ-Theme-Recovery.zip)

## Legacy 786 edition

Get KEFIRIZ for a Nintendo Switch already running **Horizon 20.5.0 / Kefir 786 / Atmosphère 1.10.0**. The release preserves Nintendo HOME and includes KEFIRIZ Homebrew, game profiles, background downloads and custom native themes.

**[Download KEFIRIZ 1.2.0](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/v1.2.0/KEFIRIZ-786-1.2.0.zip)** · [Get KEFIRJS Updater](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/v1.2.0/KEFIRJS-Updater.nro) · [Release notes](updates/1.2.0/README.md)

## Update from a phone

1. If your microSD already runs KEFIRIZ 1.1.0, download the standalone **KEFIRJS-Updater.nro** above in your phone's browser. With the console off, put it at `/switch/KEFIRJS-Updater/KEFIRJS-Updater.nro` on the card.
2. Start your usual CFW. Hold **R** while launching an installed game, then open **KEFIRJS Updater** in Homebrew.
3. The app checks for updates when it opens. Read the changes, select **Review update**, then **Install update**. Keep the microSD inserted until the updater reports completion. Reopen it to check the installed version.

If your console is offline, download the [offline update for microSD](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/v1.2.0/KEFIRJS-Offline-Update-1.2.0.zip) and extract it to the card root before opening the app. It includes the signed update, its payload and the standalone app.

## Install on a configured Kefir 786 card

Download the complete ZIP above on a phone or computer. Extract it **on Windows**, open **install.bat** and select the microSD. The installer checks the existing system and saves a rollback copy. Owners with 20.5.0 HOME exports in `/themes/systemData` also receive their available native layouts. The installer reports which screens it prepared. [Windows online installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/v1.2.0/KEFIRIZ-Installer.zip)

## Open the updater after installation

| From | Action |
| --- | --- |
| KEFIRIZ Homebrew | Open **KEFIRJS Updater** directly below Torrent. |
| Classic Homebrew Menu | Open the **KEFIRJS Updater** application. |
| KEFIRIZ System | Press **X** or tap **Updater**. |
| Nintendo HOME | Hold **R + X** while starting an installed game with the standard title-override configuration. |
| A dedicated HOME tile | An existing forwarder-capable setup can point its own shortcut at the standalone NRO path above. The Windows installer does not register a Nintendo title. |

![KEFIRJS Updater interface](media/updater-preview.jpg)

## Release files

Each release has its own folder under [updates](updates). It contains the release notes, signed metadata and on-console payload. The updater reads the signed stable feed, checks the installed edition, verifies each file, and records changes on the card.

The offline Windows archive contains the source and component notices for that build. [Updater source](updater) · [Publishing the next release](tools/README.md) · [Release history](https://github.com/Stasan-minecraft/kefirjs-updates/releases)
