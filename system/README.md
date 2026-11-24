# KEFIRIZ System

The System app reads the installed release from `sdmc:/config/kefiriz/release.txt`. It displays the Horizon and Atmosphère versions, active eMMC environment, system color, launch context and available microSD storage. X or Updater opens KEFIRIZ Updater. B, + or Close exits.

On first open with Horizon 20.5.0 and Atmosphère, System requests the running HOME menu's layout files through libnx. Available originals are saved under `config/kefiriz/native-source/20.5.0/extracted` on this console's microSD. The next Windows installation can use them to prepare the artwork. A or Refresh requests a fresh copy. The status line shows whether the cache is ready or which error Horizon returned. Source capture only writes inside the dedicated microSD cache.

The request uses `romfsMountDataStorageFromProgram`. Horizon checks the running program and the caller's content-owner access. The bundled homebrew loader declares HOME as a content owner. System also requests the User Page and Player Selection resources when Horizon makes them available. After theme recovery, wait for **HOME resources ready. On Windows run INSTALL.bat artwork.**, power off and reconnect the microSD for that command. [libnx RomFS API](https://github.com/switchbrew/libnx/blob/v4.12.0/nx/include/switch/runtime/devices/romfs_dev.h) · [Homebrew loader 2.4.5 permissions](https://github.com/switchbrew/nx-hbloader/blob/v2.4.5/hbl.json)

Build using devkitA64, libnx 4.12, switch-tools, Python and Pillow 11.0.0. This produces the native app icon and a precomputed 105-pixel emblem from the same source artwork.

```sh
python build.py --sdk /opt/devkitpro --version 2.2.1 --output KEFIRIZ-System.nro
```

Install the result at `sdmc:/switch/KEFIRIZ-System/KEFIRIZ-System.nro`. The program uses Horizon's shared font at runtime. The bundled font rasterizer is stb_truetype by Sean Barrett and contributors, at upstream commit f58f558c120e9b32c217290b80bad1a0729fbb2c. Its license accompanies its source.
