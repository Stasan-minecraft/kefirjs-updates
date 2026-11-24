# KEFIRIZ Updater

The native application checks signed KEFIRIZ releases. It obtains network access through Horizon and uses GitHub's raw content host or its contents API over verified TLS. On a failed check it identifies the service, DNS, TCP, TLS or HTTP stage. A newer, signed, complete offline kit on the card can be installed without waiting for Wi-Fi. An older or already installed offline kit cannot confirm what is on GitHub.

Press **X** or tap **Switch channel** to toggle Stable and Preview. The choice is stored independently for each firmware edition. A switch clears the previous selection and starts a fresh check. Installation always requires review and a newer matching release. Aurora uses the independent Atmosphere Preview feed. On the 786 edition Preview can track Stable while there is no newer preview.

The NRO keeps its existing SD path for launch shortcuts and older clients. On Aurora, **Y** is available if Profiles is disabled. It verifies the installed module, enables its boot flag and prompts for a reboot.

## Build

From the source directory, run the following command with the path to your devkitPro SDK. The build needs devkitA64, switch-tools, libnx and Python with Pillow. It reads the existing `stb_truetype.h` in the sibling System source tree. The packaged public verification key lives in `publisher.h`. New releases use the same public key.

```sh
python build.py --sdk /opt/devkitpro --atmosphere-edition --output KEFIRJS-Updater.nro
```

Omit `--atmosphere-edition` to build the 786 client. Place the binary at `sdmc:/switch/KEFIRJS-Updater/KEFIRJS-Updater.nro`. This canonical path works in the custom library, classic nx-hbmenu, the KEFIRIZ shortcut and an existing user-configured shortcut pointing to the same path. Installing the NRO leaves Nintendo HOME title registration under the console owner's control.

To run the navigation and confirmation-state test on a PC, build `ui.c` and `test_ui.c` together with a C11 compiler and run the result. The download and transaction engine has an independent host test. Physical hardware is required to check actual network service behavior, the controller and switching between foreground applications.

## Artwork

The Updater uses the KEFIRIZ refresh mark, two folded vermilion arrows on warm ivory. The 256 x 256 JPEG in `icon.jpg` supplies both its NRO metadata and its on-screen icon. The same mark appears in Homebrew Favorites.
