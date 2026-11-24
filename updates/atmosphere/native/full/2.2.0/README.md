# KEFIRIZ Meridian 2.2.0

Meridian is a KEFIRIZ distribution for Horizon 20.5.0 and Atmosphère 1.10.1. It uses the console's Nintendo HOME, installed games and selected storage environment. Meridian is available on **Preview**.

## What changes on the console

- **Nintendo HOME** gains a game gallery, an angular K, three groups of system controls and nine original toolbar glyphs. The game row and its touch-scroll areas move together. Each toolbar action keeps its own focus and touch target.
- **System Settings, News and All Software** use Meridian styling when the matching resources are available. Settings gains an active-category indicator, News gains a channel indicator, and the News and All Software cards have custom selection outlines. **User Page** and **Player Selection** can also receive native accents from a matching personal export. Basic White and Basic Black select the system palette. The original lock screen and game launch behavior are retained.
- **KEFIRIZ Homebrew, System and Updater** share the new visual identity. A version-specific guide opens through Profiles. Hold **R** while starting an installed game for Homebrew. **+** opens the classic menu.
- **Startup** has a Meridian Hekate icon and splash, and the full Windows installer loads new artwork into Atmosphère's documented fusee framebuffer. Later graphics depend on the boot stage. Horizon still reports its real system version.

## Install

Download and extract the Windows ZIP, run **install.bat** and select the Switch microSD. The installer backs up the boot image and builds the available screens from the console's own 20.5.0 resources. If a previous theme recovery is active, open KEFIRIZ Homebrew on the Switch, wait for its HOME resources message, power off and use **install.bat artwork** with the card reconnected. [Full Windows installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.2.0/KEFIRIZ-Meridian-2.2.0.zip)

For an existing independent installation, open **KEFIRIZ Updater → Preview** to update the applications, Profiles and Hekate images. An older updater may first install the 2.1.1 transition. Reopen it on Preview to continue. For offline installation, extract the offline kit to the microSD root, open its updater and select Preview. Personalized native layouts and the package3 splash require the Windows installer. [Offline console update](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.2.0/KEFIRIZ-Meridian-2.2.0-Offline.zip)

If HOME fails to open, power off and use **KEFIRIZ-Theme-Recovery.cmd** from the Windows ZIP. It parks the active layouts and stores them on the card. The full installer also supports **install.bat restore**.

## Verification

The installer produced all six supported screens from personal Horizon 20.5.0 resources and verified the files on a configured microSD. The four HOME-system screens preserved 1,264 original animations, and User Page and Player Selection preserved 520 more. Desktop tests covered the signed 2.1.1 upgrade, interrupted writes, restore and both app palettes. Physical controller navigation, game launch and sleep/wake still require a console test.
