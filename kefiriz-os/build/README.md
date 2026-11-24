# Build KEFIRIZ OS for Switch

KEFIRIZ OS 0.1.0 uses the Switchroot Lakka 5f29696 ARM64 hardware support package. The original kernel, firmware, display setup, dock detection and FAT32 initramfs are retained. The desktop, network control, downloads and release identity live in the new read-only Linux SYSTEM image.

## Inputs

On Windows, extract the BuildKit ZIP, open **BUILD-INSTALLER.bat** and allow the build to finish. The full Windows installer appears under **dist/KEFIRIZ-OS-0.1.0/INSTALL.bat**. Install WSL 2 with Ubuntu as the default distribution, then run this command in Ubuntu once:

```sh
sudo apt install python3 golang-go squashfs-tools gcc-aarch64-linux-gnu libc6-dev-arm64-cross ca-certificates
```

Allow 12 GiB in WSL and 4 GiB on the Windows build drive. The build downloads the official 1.29 GB hardware archive directly from Switchroot once and keeps it under **%LOCALAPPDATA%/KEFIRIZ/build-cache**. To select a different output folder or reuse an existing official archive, run `BUILD-INSTALLER.bat "C:\My builds\KEFIRIZ OS" "D:\Archives\Lakka.tar"`. Ubuntu 24.04 supplies a recent enough Go bootstrap compiler. Go 1.21+ can download the toolchain and modules pinned by the transfer service. Both the ARM64 transfer service and the desktop compile locally. The card file helper in **install/CardFiles.ps1** is the shared KEFIRIZ installer implementation from **config/kefiriz/standalone/CardFiles.ps1**, included here so this source tree builds independently.

- The builder downloads the [Switchroot Lakka archive](https://download.switchroot.org/lakka/Lakka-Switch.aarch64-5.0-devel-20240122151037-5f29696.tar) if the specified cache file is absent. Its pinned SHA-256 is `e34a6e2a8b19cb95e1b1ced10c3ef32a1c4e6a10748a4926e2a2cd6041e6daaf`. The upstream SHA-1 is `8695a9508017a16cbb2c33dc21b2d9938c3dd8db`. [Switchroot published checksums](https://download.switchroot.org/lakka/hashes.txt)
- The BSP release identifies [Lakka-LibreELEC commit 5f29696](https://github.com/libretro/Lakka-LibreELEC/commit/5f29696259313f50a215bb88b1b2d5e606ab93b5). Its original notices remain under `lakka/licenses`. Both desktop font licenses are included in `/usr/share/licenses/kefiriz` inside SYSTEM.
- Use Linux with Python 3.10+, squashfs-tools with LZO support, AArch64 GNU C and cross libc development headers. Use a native Linux scratch filesystem with at least 8 GiB free. A newer host C library works because the desktop links to the two shared libraries extracted from the pinned runtime.

## Assemble

From `kefiriz-os`, build the static ARM64 background worker with the Go version declared in `transfers/go.mod`. Keep the version in the native UI and SYSTEM consistent.

```sh
(cd transfers && CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -trimpath -buildvcs=false -ldflags="-s -w" -o ../runtime/transfer-worker .)
sudo python3 build/rootfs.py --archive /var/tmp/kefiriz-lakka.tar \
  --scratch /var/tmp --output /var/tmp/kefiriz-payload --jobs 4
```

Choose a fresh host output directory. The builder checks the upstream SHA-256 before extracting, preserves Unix metadata within the Linux staging directory, compiles the desktop, installs services and re-packs the complete root filesystem. It recalculates the FAT32 boot checksum and prints the custom SYSTEM SHA-256. The Windows installer builder consumes this payload directory.

The desktop session and download daemon run as user `Lakka` (UID 1000). The network service talks to ConnMan and accepts only UID 1000 on its local socket. The boot script mounts `lakka/storage` as writable `/storage`; settings, transfers and imported shortcuts persist there across releases.

## Validate the target build

Use Linux with `qemu-user` and the cross compiler. This exercises the compiled ARM desktop, not a desktop-platform reimplementation. The smoke test extracts the exact dynamic loader and C libraries from the completed image, drives the first-run guide using a test frontend, and verifies different frames before and after a button press.

```sh
python3 build/smoke.py /var/tmp/kefiriz-payload/lakka/SYSTEM
```

The vendor display stack requires Tegra hardware for boot, touch, controller, dock and Wi-Fi acceptance testing.

The two Hekate bitmaps can be regenerated from the original desktop mark and typography with `python3 build/boot_art.py` using Pillow and `rsvg-convert`.
