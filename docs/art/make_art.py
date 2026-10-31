#!/usr/bin/env python3
# Builds the installer's pictures and the icon files from the two source images here, made with Gemini: launcher-art.png
# (also the start screen's picture, copied next to the program by `make dist`) and icon-source.jpg. To change the art,
# replace either and run this. Needs Pillow.
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, '..', 'icon')
PINE = (13, 26, 22)


def crop_to(im, size, centre_x):
    tw, th = size
    h = im.height
    w = int(h * tw / th)
    x0 = int(min(max(centre_x * im.width - w / 2, 0), im.width - w))
    return im.crop((x0, 0, x0 + w, h)).resize((tw, th), Image.LANCZOS)


art = Image.open(os.path.join(HERE, 'launcher-art.png')).convert('RGB')
# the installer's first and last page (164 x 314 at 100%: twice that, for high-DPI screens), and its header (150 x 57)
crop_to(art, (328, 628), 0.72).save(os.path.join(HERE, 'installer-side.bmp'))
wide = art.crop((int(art.width * 0.45), int(art.height * 0.25), art.width, int(art.height * 0.25 + art.width * 0.55 * 114 / 300)))
wide.resize((300, 114), Image.LANCZOS).save(os.path.join(HERE, 'installer-header.bmp'))

# the icon: the source on a rounded tile, in every size Windows and macOS ask for
src = Image.open(os.path.join(HERE, 'icon-source.jpg')).convert('RGB')
side = min(src.size)
src = src.crop(((src.width - side) // 2, (src.height - side) // 2, (src.width + side) // 2, (src.height + side) // 2))
S = 1024
tile = src.resize((S, S), Image.LANCZOS)
mask = Image.new('L', (S * 2, S * 2), 0)
ImageDraw.Draw(mask).rounded_rectangle((32, 32, S * 2 - 32, S * 2 - 32), radius=380, fill=255)
mask = mask.resize((S, S), Image.LANCZOS)
icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
icon.paste(tile, (0, 0), mask)
icon.save(os.path.join(ICONS, 'icon_1024.png'))
icon.save(os.path.join(ICONS, 'icon.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
icon.save(os.path.join(ICONS, 'icon.icns'))
print('art done')
