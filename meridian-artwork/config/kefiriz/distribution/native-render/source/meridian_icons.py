"""Rasterize the Meridian toolbar drawings for Nintendo HOME's tinted icon materials."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw


def draw_icon(name):
    scale = 6
    mask = Image.new("L", (64 * scale, 56 * scale))
    pen = ImageDraw.Draw(mask)

    def points(coords):
        return [(int(x * scale), int(y * scale)) for x, y in coords]

    def line(*coords, width=3.6):
        pen.line(points(coords), fill=255, width=round(width * scale), joint="curve")

    def ring(*coords, width=3.6):
        line(*coords, coords[0], width=width)

    def dot(x, y, r):
        pen.ellipse((int((x-r)*scale), int((y-r)*scale), int((x+r)*scale), int((y+r)*scale)), fill=255)

    if name == "album":
        ring((12,13), (48,13), (52,17), (52,42), (16,42), (12,38))
        line((14,36), (25,25), (32,32), (37,27), (50,39))
        dot(40,21,2.8)
    elif name == "news":
        ring((14,11), (45,11), (50,16), (50,38), (46,42), (14,42))
        line((21,19), (39,19), width=4)
        line((21,26), (43,26), width=2.8)
        line((21,33), (35,33), width=2.8)
        line((9,18), (9,41), (14,46), (41,46), width=2.8)
    elif name == "shop":
        ring((15,20), (48,20), (51,40), (47,45), (16,45), (12,40))
        line((24,22), (24,15), (28,10), (36,10), (40,15), (40,22))
        line((24,35), (29,38), (39,29), width=3.2)
    elif name == "controller":
        ring((14,20), (21,15), (27,18), (37,18), (43,15), (50,20),
             (54,36), (51,41), (46,41), (39,34), (25,34), (18,41), (13,41), (10,37))
        line((18,25), (28,25), width=2.8)
        line((23,20), (23,30), width=2.8)
        dot(41,27,2)
        dot(46,23,2)
    elif name == "settings":
        for y, x in ((16,27),(28,39),(40,23)):
            line((13,y), (x-5,y), width=3.2)
            line((x+5,y), (51,y), width=3.2)
            ring((x-5,y), (x,y-5), (x+5,y), (x,y+5), width=2.8)
    elif name == "power":
        line((25,13), (18,17), (13,26), (14,34), (21,42), (31,45), (41,42),
             (49,34), (50,26), (46,18), (39,13), width=4.2)
        line((32,8), (32,29), width=4.5)
    elif name == "share":
        ring((10,23), (29,23), (32,26), (32,40), (13,40), (10,37))
        line((26,20), (26,13), (50,13), (53,16), (53,32), (36,32))
        line((23,31), (35,21), (41,21), width=3.2)
        line((35,18), (42,20), (40,26), width=2.8)
    elif name == "card":
        ring((19,10), (39,10), (47,18), (47,44), (18,44), (15,41), (15,14))
        line((38,11), (38,19), (46,19), width=2.6)
        line((22,25), (38,25), width=3.2)
        for x in (23,30,37):
            line((x,34), (x,42), width=2.8)
    elif name == "nso":
        ring((22,12), (41,12), (50,21), (50,35), (41,44), (22,44), (13,35), (13,21))
        line((13,28), (50,28), width=3)
        line((29,13), (24,21), (24,35), (29,43), width=2.8)
        line((35,13), (40,21), (40,35), (35,43), width=2.8)
    else:
        raise ValueError(name)
    image = Image.new("RGBA", (64, 56), "white")
    image.putalpha(mask.resize(image.size, Image.Resampling.LANCZOS))
    return image


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    dest = parser.parse_args().output
    dest.mkdir(parents=True, exist_ok=True)
    for key in ("album", "news", "shop", "controller", "settings", "power", "share", "card", "nso"):
        image = draw_icon(key)
        path = dest / (key + ".dds")
        image.save(path, "DDS", pixel_format="DXT5")
        data = path.read_bytes()
        if len(data) != 3712 or data[84:88] != b"DXT5":
            raise ValueError("The HOME textures require the DXT5 encoder in Pillow 11.3 or newer")
        with Image.open(path) as packed:
            assert packed.size == image.size
            assert packed.convert("RGBA").getchannel("A").getextrema() == (0, 255)
