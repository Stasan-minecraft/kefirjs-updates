# KEFIRJS Updater

This standalone Nintendo Switch homebrew application checks the stable release feed for KEFIRIZ Horizon edition 786. Open **KEFIRJS Updater** in Homebrew. KEFIRIZ 1.2.0 also exposes it in KEFIRIZ System with **X**, or directly by holding **R + X** while opening an installed title.

It first checks the SD card for an interrupted transaction, then retrieves the latest release. Press **A** to review the update and **A** again to install the selected version. **B** on the confirmation screen returns to the notes. Longer notes can be paged with the D-pad or the left stick. The buttons and paging controls also accept touch. After installation, close the app to use the updated Homebrew menu and applications. Both Homebrew memory modes use the same update flow. The graphical interface follows Basic White and Basic Black. A text interface with the same controller actions is used if the framebuffer or shared font is unavailable.

Release 1.2.0 upgrades an existing KEFIRIZ 1.1.0 card on Horizon 20.5.0. The program validates the signed feed and checks the downloaded files before committing the update. The offline and Windows packages are listed on the project release page. [KEFIRJS Updates](https://github.com/Stasan-minecraft/kefirjs-updates)

## Build

From the source directory, run the following command with the path to your devkitPro SDK. The build needs devkitA64, switch-tools, libnx and Python with Pillow. It reads the existing `stb_truetype.h` in the sibling System source tree. The packaged public verification key lives in `publisher.h`. New releases use the same public key.

```sh
python build.py --sdk /opt/devkitpro --output KEFIRJS-Updater.nro
```

Place the binary at `sdmc:/switch/KEFIRJS-Updater/KEFIRJS-Updater.nro`. This canonical path works in the custom library, classic nx-hbmenu, the KEFIRIZ shortcut and an existing user-configured shortcut pointing to the same path. Installing the NRO leaves Nintendo HOME title registration under the console owner's control.

To run the navigation and confirmation-state test on a PC, build `ui.c` and `test_ui.c` together with a C11 compiler and run the result. The download and transaction engine has an independent host test. Physical hardware is required to check actual network service behavior, the controller and switching between foreground applications.

## Artwork

The built-in ImageGen tool generated the firmware update icon. The `icon.jpg` file is a 256 × 256 RGB JPEG used in the NRO metadata and its on-screen update panel.

> Use case: stylized-concept. Square high-end handheld firmware update app icon. A single physical object, a wide folded scarlet metal ribbon shaped into an open circular update arrow rotating clockwise, arrowhead at 1 o'clock, seen straight on, centered, taking 74 percent of the square. Cherry red enamel, one brighter coral facet and fine charcoal shadow, large bold silhouette legible on a tiny device screen. Full bleed warm pearl-white untextured background, soft studio light. No text, no marks, no secondary symbols, no device frame, no border, no rounded icon container. Symmetrical visual balance.

Build with `--atmosphere-edition` for KEFIRIZ 2.0.0 on official Atmosphere 1.10.1. It reads the independently signed feed in `updates/atmosphere`, uses a separate recovery journal, and requires the matching edition and runtime.
