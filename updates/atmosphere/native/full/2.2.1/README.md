# Meridian 2.2.1 Preview

On Horizon 20.5.0, Meridian can open a game downloaded by KEFIRIZ OS on the same microSD. This works with the built-in catalog releases of 2048, Minesweeper and SudokuNX.

1. Finish the download in KEFIRIZ OS. Open **Power → Restart to boot menu** and choose **KEFIRIZ Meridian**.
2. Hold **R** while starting an installed title to open KEFIRIZ Homebrew. Open **KEFIRIZ Torrent**, choose the finished game and press **A**.

Meridian checks the completion record, file size and publisher's SHA-256. A verified local Meridian copy takes priority when both systems have the game. The Linux copy can launch directly from its original location. Downloads pause when switching operating systems.

## Installation

On Meridian, open **KEFIRIZ Updater → Preview** and install the signed update. The offline update ZIP also works with a phone. Extract it to the microSD and open the updater on the console. The Windows installer includes Ember artwork and its per-revision undo command. This release uses the existing Atmosphère 1.10.1 runtime and Horizon 20.5.0.

[Windows installer](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.2.1/KEFIRIZ-Meridian-2.2.1.zip) · [Offline update](https://github.com/Stasan-minecraft/kefirjs-updates/releases/download/atmosphere-v2.2.1/KEFIRIZ-Meridian-2.2.1-Offline.zip)

For a card displaying 2.1.1 with the 2.2.0 Profiles module already installed, extract the entire offline ZIP, including its updater, then select Preview. This mixed installation was tested directly.

## Verification

The ARM64 Rain worker verified the three publisher files. Meridian's production catalog resolver and NRO reader opened each verified file in a host test. Tests with the published 2.2.0 updater and installer covered an interrupted update, SHA-256 verification of all eight signed files, and recovery. Physical game launch and controller testing are pending.
