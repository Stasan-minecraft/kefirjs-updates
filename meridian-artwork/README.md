# KEFIRIZ Meridian Ember artwork

This Windows package applies Ember to an existing KEFIRIZ Meridian installation on Horizon 20.5.0. It builds native HOME, All Software, News, Settings, User Page and Player Selection artwork from the matching layout resources available on your microSD. Basic White and Basic Black control the palette. The system lock screen uses its original design.

1. Extract the ZIP to your PC. Power off your Switch and connect its microSD.
2. Open **INSTALL.bat**, enter the microSD drive letter and wait for the list of updated screens. From a terminal you can run `INSTALL.bat artwork E:\` using your card’s drive letter.
3. Safely eject the card and start Meridian. To try both palettes, open **System Settings → Themes**.

If the installer requests a matching HOME export, start Meridian, open **KEFIRIZ System** and refresh the HOME resources. When they are ready, power off, reconnect the card and repeat the installation.

Run `INSTALL.bat undo-artwork E:\` to return to the previous managed artwork revision. The installer verifies existing artwork and backup hashes before reverting one revision. After an interrupted undo, run the same command again. If only the initial artwork is installed, the command leaves it active.

The Windows renderer, its source, build script and license notices are in `config/kefiriz/distribution/native-render`.
