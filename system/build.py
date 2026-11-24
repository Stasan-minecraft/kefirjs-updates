"""Build KEFIRIZ System with devkitPro, libnx 4.12+ and switch-tools."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from math import isqrt

from PIL import Image

SOURCE = Path(__file__).resolve().parent


def prepare_emblem(work):
    size = 105
    with Image.open(work / "icon.jpg") as image:
        pixels = image.convert("RGB").resize((size, size), Image.Resampling.LANCZOS)
        indexed = pixels.quantize(colors=128)
    colors = indexed.getpalette()
    palette = [0xff000000 | colors[i] | colors[i + 1] << 8 | colors[i + 2] << 16
               for i in range(0, len(colors), 3)]
    radius = 14
    inset = [radius - isqrt(radius**2 - max(0, radius - 1 - min(y, size - y - 1))**2)
             for y in range(size)]
    rows = ["static const uint32_t emblem_palette[] = {" + ", ".join(hex(c) for c in palette) + "};",
            "static const uint8_t emblem_inset[] = {" + ", ".join(map(str, inset)) + "};",
            f"static const uint8_t emblem_pixels[{size}][{size}] = {{"]
    data = indexed.tobytes()
    rows.extend("{" + ", ".join(map(str, data[y:y + size])) + "}," for y in range(0, len(data), size))
    (work / "emblem.h").write_text("\n".join([*rows, "};", ""]), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=os.environ.get("DEVKITPRO"),
                        required="DEVKITPRO" not in os.environ)
    parser.add_argument("--output", type=Path, default=Path("KEFIRIZ-System.nro"))
    parser.add_argument("--version", default="2.2.1")
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    nx = sdk / "libnx"
    suffix = ".exe" if os.name == "nt" else ""
    gcc = sdk / "devkitA64/bin" / ("aarch64-none-elf-gcc" + suffix)
    tools = sdk / "tools/bin"
    with tempfile.TemporaryDirectory(prefix="kefiriz-system-") as tmp:
        work = Path(tmp)
        for name in ("system.c", "system_ui.c", "system_ui.h", "native_source.c", "native_source.h", "icon.jpg"):
            shutil.copyfile(SOURCE / name, work / name)
        prepare_emblem(work)
        (work / "vendor").mkdir()
        shutil.copyfile(SOURCE / "vendor/stb_truetype.h", work / "vendor/stb_truetype.h")
        env = dict(os.environ, DEVKITPRO=sdk.as_posix())
        subprocess.run([str(gcc), "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft",
                        "-fPIE", "-ffunction-sections", "-fdata-sections", "-D__SWITCH__",
                        "-I" + (nx / "include").as_posix(), "-L" + (nx / "lib").as_posix(),
                        "-specs=" + (nx / "switch.specs").as_posix(), "system.c", "system_ui.c", "native_source.c",
                        "-o", "system.elf", "-lnx", "-lm"], cwd=work, env=env, check=True)
        subprocess.run([str(tools / ("nacptool" + suffix)), "--create", "KEFIRIZ System",
                        "KEFIRIZ", args.version, "system.nacp"], cwd=work, check=True)
        subprocess.run([str(tools / ("elf2nro" + suffix)), "system.elf", "system.nro",
                        "--icon=icon.jpg", "--nacp=system.nacp"], cwd=work, check=True)
        nro = (work / "system.nro").read_bytes()
        mod = struct.unpack_from("<I", nro, 4)[0]
        asset = struct.unpack_from("<I", nro, 0x18)[0]
        if (nro[0x10:0x14] != b"NRO0" or nro[mod+0x34:mod+0x38] != b"LNY2" or
                struct.unpack_from("<I", nro, mod+0x38)[0] < 1 or nro[asset:asset+4] != b"ASET"):
            raise ValueError("Unexpected NRO or libnx ABI")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, delete=False) as dest:
            pending = Path(dest.name)
        try:
            pending.write_bytes(nro)
            os.replace(pending, args.output)
        finally:
            pending.unlink(missing_ok=True)
        print(f"{args.output.name}: {len(nro)} bytes, SHA-256 {hashlib.sha256(nro).hexdigest()}")


if __name__ == "__main__":
    main()
