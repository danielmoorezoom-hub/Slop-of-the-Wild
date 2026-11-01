# The front page's banner: the launcher's art, darkened toward the left, with the icon and the wordmark.
# usage: make_banner.py <out.png>
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W, H = 1600, 480
GOLD = (232, 214, 160)
MOSS = (190, 226, 84)


def font(size):
    return ImageFont.truetype(os.path.join(ROOT, 'third_party', 'fonts', 'CinzelDecorative', 'CinzelDecorative-Bold.ttf'), size)


art = Image.open(os.path.join(HERE, 'launcher-art.png')).convert('RGB')
k = max(W / art.width, H / art.height)
art = art.resize((round(art.width * k), round(art.height * k)), Image.LANCZOS)
x, y = (art.width - W) // 2, (art.height - H) // 2
art = art.crop((x, y, x + W, y + H))

# dark on the left for the title, opening up to the art on the right
shade = Image.new('L', (W, H))
px = shade.load()
for i in range(W):
    a = int(225 * max(0.0, min(1.0, 1.15 - i / (W * 0.72))))
    for j in range(H):
        px[i, j] = a
base = Image.composite(Image.new('RGB', (W, H), (10, 14, 12)), art, shade)

icon = Image.open(os.path.join(HERE, 'icon-source.jpg')).convert('RGB')
s = 240
icon = icon.resize((s, s), Image.LANCZOS)
mask = Image.new('L', (s * 4, s * 4), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, s * 4 - 1, s * 4 - 1), radius=s * 4 // 6, fill=255)
mask = mask.resize((s, s), Image.LANCZOS)
ix, iy = 70, (H - s) // 2
glow = Image.new('L', (W, H), 0)
glow.paste(mask, (ix, iy))
glow = glow.filter(ImageFilter.GaussianBlur(18))
base = Image.composite(Image.new('RGB', (W, H), (70, 96, 40)), base, glow.point(lambda v: v // 2))
base.paste(icon, (ix, iy), mask)

d = ImageDraw.Draw(base)
tx = ix + s + 56


def text(xy, t, f, fill):
    layer = Image.new('L', (W, H), 0)
    ImageDraw.Draw(layer).text(xy, t, font=f, fill=200)
    return layer.filter(ImageFilter.GaussianBlur(5)), t, f, fill, xy


title = font(92)
sub = font(92)
for xy, t, f in (((tx, 128), 'Slop of', title), ((tx, 232), 'the Wild', sub)):
    shadow, *_ = text((xy[0] + 3, xy[1] + 4), t, f, GOLD)
    base = Image.composite(Image.new('RGB', (W, H), (0, 0, 0)), base, shadow)
d = ImageDraw.Draw(base)
d.text((tx, 128), 'Slop of', font=title, fill=GOLD)
d.text((tx, 232), 'the Wild', font=sub, fill=GOLD)
base.save(sys.argv[1])
print(sys.argv[1], base.size)
