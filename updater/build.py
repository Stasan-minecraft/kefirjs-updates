"""Build the KEFIRIZ Updater homebrew NRO with devkitPro/libnx and Pillow."""
import argparse
from hashlib import sha256
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from PIL import Image

HERE = Path(__file__).resolve().parent


def icon_header(directory):
    with Image.open(HERE / "icon.jpg") as image:
        square = image.convert("RGB").resize((112, 112), Image.Resampling.LANCZOS)
        rgb565 = [(r >> 3) << 11 | (g >> 2) << 5 | (b >> 3) for r, g, b in square.getdata()]
    rows = [", ".join(f"0x{x:04x}" for x in rgb565[i:i + 16]) for i in range(0, len(rgb565), 16)]
    (directory / "icon_pixels.h").write_text(
        "static const unsigned short up_icon[112 * 112] = {\n" + ",\n".join(rows) + "\n};\n",
        encoding="ascii",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=os.getenv("DEVKITPRO") or "C:/devkitPro")
    parser.add_argument("--output", type=Path, default=Path("KEFIRJS-Updater.nro"))
    parser.add_argument("--atmosphere-edition", action="store_true")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    nx = sdk / "libnx"
    suffix = ".exe" if os.name == "nt" else ""
    tools = sdk / "tools/bin"
    cc = sdk / "devkitA64/bin" / ("aarch64-none-elf-gcc" + suffix)
    sources = ["main.c", "draw.c", "ui.c", "engine.c", "engine_nx.c",
               "crypto/monocypher.c", "crypto/monocypher-ed25519.c"]
    font = next((d / "vendor/stb_truetype.h" for d in (HERE.parent / "system-src", HERE.parent / "system")
                 if (d / "vendor/stb_truetype.h").is_file()), None)
    if font is None:
        raise FileNotFoundError("The source/system shared stb_truetype header is required")
    with tempfile.TemporaryDirectory(prefix="kefirjs-updater-") as tmp:
        work = Path(tmp)
        for file in HERE.glob("*"):
            if file.suffix in (".c", ".h", ".inc"):
                shutil.copyfile(file, work / file.name)
        shutil.copytree(HERE / "crypto", work / "crypto")
        shutil.copyfile(font, work / font.name)
        shutil.copyfile(HERE / "icon.jpg", work / "icon.jpg")
        icon_header(work)
        env = dict(os.environ, DEVKITPRO=sdk.as_posix())
        flags = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-march=armv8-a+crc+crypto",
                 "-mtune=cortex-a57", "-mtp=soft", "-fPIE", "-ffunction-sections", "-fdata-sections",
                 "-D__SWITCH__", "-I.", "-I" + (nx / "include").as_posix(),
                 "-L" + (nx / "lib").as_posix(), "-specs=" + (nx / "switch.specs").as_posix()]
        if args.atmosphere_edition:
            flags.append("-DKF_ATMOSPHERE_EDITION")
        subprocess.run([cc, *flags, *sources, "-o", "updater.elf", "-lnx", "-lm"],
                       env=env, cwd=work, check=True)
        name = "KEFIRIZ Updater"
        subprocess.run([tools / ("nacptool" + suffix), "--create", name, "KEFIRIZ",
                        "2.1.0" if args.atmosphere_edition else "1.2.3", "updater.nacp"], cwd=work, check=True)
        subprocess.run([tools / ("elf2nro" + suffix), "updater.elf", "updater.nro",
                        "--icon=icon.jpg", "--nacp=updater.nacp"], cwd=work, check=True)
        binary = (work / "updater.nro").read_bytes()
        aset = struct.unpack_from("<I", binary, 24)[0]
        magic, asset_version, icon_pos, icon_len, nacp_pos, nacp_len = struct.unpack_from("<4sI4Q", binary, aset)
        if (binary[16:20] != b"NRO0" or magic != b"ASET" or asset_version != 0 or
                binary[aset + icon_pos:aset + icon_pos + icon_len] != (work / "icon.jpg").read_bytes() or
                nacp_len != 0x4000 or binary[aset + nacp_pos:aset + nacp_pos + len(name)] != name.encode("ascii")):
            raise ValueError("Invalid NRO or missing updater name and icon")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as stage:
        stage.write(binary)
        pending = Path(stage.name)
    try:
        os.replace(pending, args.output)
    finally:
        pending.unlink(missing_ok=True)
    print(f"{args.output.name}: {len(binary):,} bytes, SHA-256 {sha256(binary).hexdigest()}")


if __name__ == "__main__":
    main()
