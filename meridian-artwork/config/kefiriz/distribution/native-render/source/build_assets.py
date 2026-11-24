"""Preconvert KEFIRIZ art with Pillow 11.3.0 and SwitchThemes 4.8.3 (build time)."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--themes", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    tool = args.tool.resolve()
    if hashlib.sha256(tool.read_bytes()).hexdigest() != "7280cf72a1f9c577f4954945fcc238b6d23c01dc4743292d423808b3ac5f99fa":
        raise ValueError("Expected the verified SwitchThemes 4.8.3 Windows tool")
    for palette in ("Dark", "Light"):
        for part in ("home", "lock", "apps", "set", "news", "psl", "user"):
            expected = {"image.jpg"}
            if part == "home":
                expected.update(f"{x}.png" for x in ("album", "card", "controller", "news", "nso", "power", "settings", "share", "shop"))
            if part == "lock":
                expected.add("lock.png")
            with tempfile.TemporaryDirectory() as temp:
                unpacked = Path(temp)
                theme = args.themes / palette / (part + ".nxtheme")
                result = subprocess.run([tool, "extract", theme.resolve(), unpacked], capture_output=True, check=True)
                if b"Done!" not in result.stdout or result.stderr:
                    raise ValueError(f"Theme extraction failed for {theme}")
                if {file.name for file in unpacked.iterdir()} != expected:
                    raise ValueError(f"Unexpected theme artwork in {theme}")
                dest = args.out / palette / part
                dest.mkdir(parents=True, exist_ok=True)
                for name in sorted(expected):
                    background = name == "image.jpg"
                    size = (1280, 720) if background else ((184, 168) if part == "lock" else (64, 56))
                    codec = "DXT1" if background else "DXT5"
                    with Image.open(unpacked / name) as image:
                        if image.size != size:
                            raise ValueError(f"Unexpected {name} dimensions in {theme}")
                        output = dest / (Path(name).stem + ".dds")
                        image.convert("RGB" if background else "RGBA").save(output, "DDS", pixel_format=codec)
                    data = output.read_bytes()
                    if data[84:88] != codec.encode() or len(data) != 128 + size[0]*size[1] // (2 if background else 1):
                        raise ValueError("Expected a single-level compressed DDS texture")


if __name__ == "__main__":
    main()
