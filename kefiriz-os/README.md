# KEFIRIZ OS 0.2.0

KEFIRIZ OS provides an English-language desktop for Switch. Its own executable draws to Xorg, reads Linux controller events and handles touch, keyboard and display resizing. Linux, board support and display drivers come from Switchroot L4T Lakka build 5f29696. Select **KEFIRIZ Meridian** in Hekate to play installed Switch games.

## On the console

- Select **KEFIRIZ OS** in Hekate. Each release has a first-run guide. Open it again under **Settings в†’ Feature guide**.
- Press **R + ZR** in any desktop window for Quick settings. Change the current profile's appearance or clock, check free storage, or enter the transfer queue. Press the shortcut again to return to the window you were using.
- Move with the directional buttons, press **A** to open and **B** to return. Tap a tile or dialog row to select it. **Plus** assigns a feature to the selected HOME tile or dock button. **Settings в†’ Customize dock** gives access to all six positions. **X** switches among three local profiles.
- In **Settings**, choose Graphite or Porcelain, set the clock format, or open **Wi-Fi**. Scan, choose an open or personal-password network, then enter the password using the on-screen keyboard. Connection availability depends on the Switch wireless driver and access point.
- **Torrent** includes three publisher-sourced Switch homebrew downloads. It also accepts compatible BitTorrent v1 metadata from the inbox. The independent transfer service keeps downloading in other KEFIRIZ OS windows and resumes when Linux boots again. Internet downloads pause while Meridian is running. Downloaded Switch homebrew is for Meridian, and importing a torrent stores its files without installing a game title.
- Press **Y** to capture a screen, then open **Album** to view it. **Power в†’ Restart to boot menu** returns to Hekate. Choose **KEFIRIZ Meridian** for installed Switch games.

## Your own actions

On the microSD, the Linux user directory is **lakka/storage/kefiriz**. Inside KEFIRIZ OS it is mounted at **/storage/kefiriz**. HOME creates the subdirectories on first launch.

Save a UTF-8 shell script with Linux line endings under **actions**. Its filename, minus the .sh extension, becomes its title in the Library and button picker. Use letters, digits, spaces, underscores, hyphens or dots and keep the filename below 72 bytes. For example, **actions/Free space.sh** can contain

```sh
#!/bin/sh
df -h /storage | awk 'NR == 2 {print $4 " free on microSD"}'
```

Open the Library or press Plus on a tile to refresh the list. Assign the new action to any tile or dock button. KEFIRIZ runs it using the desktop user's permissions and displays its first output line. Only put scripts you intend to run in this directory.

Place .torrent files in **inbox**. Completed files live under **downloads** in a separate directory for each transfer. The album stores standard PPM files in **album**. Each profile has its own tile assignments, dock, palette and clock format.

## Install and restore

For the startup message **Board was not initialized properly! Hang prevented.**, use the Windows boot tools to check the card and identify the stage that stopped. The same tools configure a normal restart from Linux back to Hekate. [Boot diagnostics](install/README.md#boot-diagnostics)

For an existing 0.1.0 installation, the installer preserves a rollback copy of the Linux image. Run `INSTALL.bat RollbackUpdate E` to return to that image.

The Windows release contains **INSTALL.bat** and a verified FAT32 payload. Open it from the extracted **KEFIRIZ-OS-0.2.0** directory. Its install, status, verify and restore commands are documented in the [installer guide](install/README.md). Installation uses files on the existing FAT32 volume. To boot the original menu configuration again, run `INSTALL.bat restore E` from that package, substituting the card's actual letter. Restore preserves the Linux data in an archive directory on the card.

## Build

On Windows, extract the public **BuildKit** ZIP and open **BUILD-INSTALLER.bat** inside its kefiriz-os folder. It uses the default Ubuntu distribution in WSL 2, downloads the pinned Switchroot archive, and writes the finished installer to **dist/KEFIRIZ-OS-0.2.0/INSTALL.bat**. Reserve 12 GiB in WSL and 4 GiB on the Windows build drive. Run `BUILD-INSTALLER.bat --help` for the exact Ubuntu prerequisites and optional output and cache paths. [Public build kit](https://github.com/Stasan-minecraft/kefirjs-updates/releases/tag/kefiriz-os-v0.2.0)

Use Linux with Python 3.10+, squashfs-tools with LZO, an AArch64 Linux compiler, a Go toolchain supporting the version in transfers/go.mod, and host GCC. The image builder checks the official upstream archive against its pinned SHA-256. Install the X11 and XRender development headers on the build host. Use new output directories. The working and extracted files need several GB on a Linux filesystem.

```sh
python3 build/worker.py
python3 tests/test_core.py --sanitize
sudo python3 build/rootfs.py --archive /var/tmp/kefiriz-upstream.tar --output /var/tmp/kefiriz-payload --scratch /var/tmp
python3 install/prepare_release.py --version 0.2.0 --payload /var/tmp/kefiriz-payload --upstream-archive /var/tmp/kefiriz-upstream.tar --output /var/tmp/KEFIRIZ-OS-0.2.0 --zip /var/tmp/KEFIRIZ-OS-0.2.0.zip
```

The upstream board files and kernel are pinned. The custom root filesystem includes the desktop, wireless and transfer services, boot art and versioned system identity. The renderer uses Barlow and PT Sans. Their notices, the Go license and the dependency notices ship with the worker. The linked libretro API header carries its upstream license.

[Switchroot installation and hardware requirements](https://wiki.switchroot.org/wiki/linux/l4t-lakka-install-update-guide) В· [Lakka on the Switch](https://www.lakka.tv/doc/Nintendo-Switch/) В· [Rain BitTorrent engine](https://github.com/cenkalti/rain)

