#!/usr/bin/env python3
# The launcher's and the installer's placeholder art, drawn from numbers (no pictures of the game): wind over layered ridges
# at dusk, in the program's colours. Writes docs/art/. To change the art, replace those files: docs/art/launcher-art.png is
# copied next to the program by `make dist`, and the installer takes its two .bmp files from there (`make installer`).
# usage: make_art.py [out folder]   (needs numpy and Pillow)
import math
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

W, H = 1920, 1080
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
rng = np.random.default_rng(1996)

# the colours (runtime/ui/launcher.mm)
PINE = np.array([13, 26, 22], float)
SKY_TOP = np.array([18, 38, 52], float)
SKY_MID = np.array([66, 92, 84], float)
SKY_LOW = np.array([236, 176, 96], float)
ACCENT = np.array([190, 226, 84], float)


def noise1d(n, octaves, base, gain=0.5):
    out = np.zeros(n)
    amp = 1.0
    for o in range(octaves):
        k = base * 2 ** o
        pts = rng.random(k + 2)
        x = np.linspace(0, k, n)
        i = x.astype(int)
        f = x - i
        f = f * f * (3 - 2 * f)
        out += amp * (pts[i] * (1 - f) + pts[i + 1] * f)
        amp *= gain
    return out / out.max()


def noise2d(w, h, cells):
    g = rng.random((cells[1] + 2, cells[0] + 2))
    x = np.linspace(0, cells[0], w)
    y = np.linspace(0, cells[1], h)
    xi, yi = x.astype(int), y.astype(int)
    fx, fy = x - xi, y - yi
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a = g[yi][:, xi] * (1 - fx) + g[yi][:, xi + 1] * fx
    b = g[yi + 1][:, xi] * (1 - fx) + g[yi + 1][:, xi + 1] * fx
    return a * (1 - fy[:, None]) + b * fy[:, None]


yy, xx = np.mgrid[0:H, 0:W].astype(float)
v = yy / H

# the sky: night blue above, a warm band low down, over to the right where the sun has just gone
sky = np.zeros((H, W, 3))
t1 = np.clip(v / 0.42, 0, 1)[..., None]
t2 = np.clip((v - 0.30) / 0.34, 0, 1)[..., None] ** 1.4
sky = SKY_TOP * (1 - t1) + SKY_MID * t1
sky = sky * (1 - t2) + SKY_LOW * t2
glow = np.exp(-(((xx - W * 0.68) / (W * 0.30)) ** 2 + ((yy - H * 0.60) / (H * 0.20)) ** 2))[..., None]
sky += glow * np.array([70, 44, 12])
cloud = sum(noise2d(W, H, (c, c // 3 + 1)) * a for c, a in ((5, 1), (11, .5), (23, .25), (47, .12))) / 1.87
cloud = np.clip((cloud - 0.5) * 3.2, 0, 1) * np.clip(1 - np.abs(v - 0.34) / 0.3, 0, 1)
img = sky * (1 - cloud[..., None] * 0.34) + (SKY_LOW * 0.9 + 22) * cloud[..., None] * 0.34

# ridges, far to near
layers = [
    (0.60, 0.11, 5, 7, [52, 74, 72]),
    (0.67, 0.13, 4, 7, [36, 60, 54]),
    (0.75, 0.13, 3, 7, [25, 49, 40]),
    (0.84, 0.13, 3, 6, [17, 37, 30]),
    (0.93, 0.12, 2, 6, [PINE[0], PINE[1], PINE[2]]),
]
ridge_top = []
for i, (base, height, cells, oct_, col) in enumerate(layers):
    top = H * (base - height * (noise1d(W, oct_, cells) - 0.5) * 2)
    ridge_top.append(top)
    mask = np.clip(yy - top[None, :], 0, 1)
    depth = np.clip((yy - top[None, :]) / (H * 0.3), 0, 1)
    c = np.array(col, float)
    shade = c[None, None, :] * (1 - depth[..., None] * 0.35)
    # a warm rim on the sunward side, and the haze in the valley behind it
    rim = np.exp(-(yy - top[None, :]).clip(0) / 5.0) * np.exp(-(((xx - W * 0.68) / (W * 0.42)) ** 2)) * (0.75 - i * 0.12)
    shade = shade + rim[..., None] * np.array([120, 92, 40])
    img = img * (1 - mask[..., None]) + shade * mask[..., None]
    if i < len(layers) - 1:
        haze = np.exp(-((yy - top[None, :] - H * 0.04).clip(0)) / (H * 0.05)) * mask * 0.22
        img = img * (1 - haze[..., None]) + (SKY_LOW * 0.55 + 30) * haze[..., None]

# the wind: long drifting strokes in the accent colour, fading at their ends. Drawn into their own layer, softened, added.
wind = Image.new('L', (W, H), 0)
px = np.zeros((H, W))
for s in range(20):
    x0 = rng.uniform(W * 0.15, W * 0.95)
    y0 = rng.uniform(H * 0.24, H * 0.60)
    length = int(rng.uniform(420, 1100))
    amp = rng.uniform(5, 20)
    wave = rng.uniform(420, 900)
    ph = rng.uniform(0, 6.28)
    rise = rng.uniform(-0.09, -0.02)
    weight = rng.uniform(0.25, 1.0)
    for k in range(length):
        x = x0 + k
        y = y0 + rise * k + amp * math.sin(ph + k / wave * 6.28)
        if not (0 <= x < W - 1 and 0 <= y < H - 1):
            continue
        f = math.sin(math.pi * k / length) ** 1.5
        px[int(y), int(x)] += f * weight
wind = Image.fromarray(np.clip(px * 255, 0, 255).astype('uint8'))
soft = np.asarray(wind.filter(ImageFilter.GaussianBlur(1.1)), float) / 255
wide = np.asarray(wind.filter(ImageFilter.GaussianBlur(7)), float) / 255
w = np.clip(soft * 1.5 + wide * 2.2, 0, 1)
img = img + w[..., None] * (ACCENT * 0.8 + 40) * 0.62

# seeds on the wind, near the middle distance
for s in range(170):
    x, y = rng.uniform(W * 0.2, W), rng.uniform(H * 0.25, H * 0.95)
    r = rng.uniform(1.0, 2.8)
    a = rng.uniform(0.25, 0.9)
    x0, x1, y0, y1 = int(x - 6), int(x + 7), int(y - 6), int(y + 7)
    if x0 < 0 or y0 < 0 or x1 >= W or y1 >= H:
        continue
    d = np.exp(-(((xx[y0:y1, x0:x1] - x) ** 2 + (yy[y0:y1, x0:x1] - y) ** 2) / (r * r)))
    img[y0:y1, x0:x1] += d[..., None] * np.array([240, 236, 190]) * a

# a fine grain keeps the gradients from banding on any screen
img += rng.normal(0, 1.6, (H, W, 1))
art = Image.fromarray(np.clip(img, 0, 255).astype('uint8'))

os.makedirs(OUT, exist_ok=True)
art.save(os.path.join(OUT, 'launcher-art.png'), optimize=True)


def crop_to(size, centre_x, zoom):
    tw, th = size
    ch = int(H / zoom)
    cw = int(ch * tw / th)
    x0 = int(min(max(centre_x * W - cw / 2, 0), W - cw))
    y0 = H - ch
    return art.crop((x0, y0, x0 + cw, y0 + ch)).resize((tw, th), Image.LANCZOS)


def fade(im, side, to=PINE):
    a = np.asarray(im, float)
    h, w, _ = a.shape
    if side == 'bottom':
        f = np.clip((np.arange(h) / h - 0.55) / 0.45, 0, 1)[:, None, None]
    else:
        f = np.clip(1 - (np.arange(w) / w) / 0.55, 0, 1)[None, :, None]
    return Image.fromarray(np.clip(a * (1 - f) + to * f, 0, 255).astype('uint8'))


# the installer's first and last page (164 x 314 at 100%: twice that, for high-DPI screens), and its header (150 x 57)
side = fade(crop_to((328, 628), 0.70, 1.0), 'bottom')
side.convert('RGB').save(os.path.join(OUT, 'installer-side.bmp'))
head = fade(crop_to((300, 114), 0.68, 1.3), 'left')
head.convert('RGB').save(os.path.join(OUT, 'installer-header.bmp'))
print('art:', OUT)
