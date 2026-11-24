"""Render the two Hekate bitmaps from the same originals as the desktop."""
import argparse
import io
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont

UI = Path(__file__).resolve().parents[1] / "ui"
SVG = "http://www.w3.org/2000/svg"


def main():
    options = argparse.ArgumentParser(description=__doc__)
    options.add_argument("--rsvg", default=shutil.which("rsvg-convert") or "rsvg-convert")
    args = options.parse_args()
    ET.register_namespace("", SVG)

    def render(source, width, height):
        result = subprocess.run([args.rsvg, "-w", str(width), "-h", str(height)], input=source,
                                check=True, stdout=subprocess.PIPE)
        return Image.open(io.BytesIO(result.stdout)).convert("RGBA")

    sprite = ET.parse(UI / "icons.svg").getroot()
    mark = next(child for child in sprite if child.get("id") == "mark")
    svg = ET.Element(f"{{{SVG}}}svg", {**sprite.attrib, "viewBox": mark.get("viewBox"), "color": "#ef745e"})
    svg.extend(list(mark))
    mark_svg = ET.tostring(svg)
    background = Image.new("RGBA", (1280, 720), "#202425")
    contours = render((UI / "contours.svg").read_bytes(), 1280, 720)
    background.alpha_composite(contours)
    background.alpha_composite(render(mark_svg, 126, 110), (362, 298))
    font = ImageFont.truetype(str(UI / "font/Barlow-SemiBold.ttf"), 112)
    ImageDraw.Draw(background).text((504, 350), "KEFIRIZ OS", font=font, fill="#f6f3eb", anchor="lm")
    # The Switch panel is mounted in portrait orientation.
    destination = UI / "boot"
    destination.mkdir(exist_ok=True)
    background.transpose(Image.Transpose.ROTATE_90).save(destination / "kefiriz-logo.bmp")
    tile = Image.new("RGBA", (192, 192), "#202425")
    tile.alpha_composite(render(mark_svg, 136, 119), (35, 35))
    tile.save(destination / "kefiriz-icon.bmp")


if __name__ == "__main__":
    main()
