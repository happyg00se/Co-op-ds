#!/usr/bin/env python3
"""Procedural art for DSCoopSetup.exe (original artwork, no game assets).

Generates:
  assets/installer/hero.jpg   1380x870 (1.5x of the 920x580 window): misty ridges, clouds,
                              faint inverted rainbow, two porters walking together on a ridge.
  assets/installer/icon.ico   app icon (golden chiral ring), 16..256 px
  assets/installer/nodes.txt  design-space (920x580) points on the ridges for the animated network
Deterministic: same seed -> same pixels. Run: python3 tools/art/gen_installer_art.py
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "installer")
W, H = 1380, 870
DESIGN_SCALE = 1.5  # art pixels per window design unit
rng = np.random.default_rng(1850570)


# ----------------------------------------------------------------------------- noise
def value_noise_1d(x, seed):
    g = np.random.default_rng(seed).random(8192)
    xi = np.floor(x).astype(np.int64)
    xf = x - xi
    u = xf * xf * (3 - 2 * xf)
    return g[xi % 8192] * (1 - u) + g[(xi + 1) % 8192] * u


def fbm1d(x, seed, octaves=6, lac=2.0, gain=0.5):
    total, amp, freq, norm = np.zeros_like(x, dtype=np.float64), 1.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise_1d(x * freq, seed + o * 101)
        norm += amp
        amp *= gain
        freq *= lac
    return total / norm


def value_noise_2d(x, y, seed, size=512):
    g = np.random.default_rng(seed).random((size, size))
    xi, yi = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    xf, yf = x - xi, y - yi
    u, v = xf * xf * (3 - 2 * xf), yf * yf * (3 - 2 * yf)
    x0, x1, y0, y1 = xi % size, (xi + 1) % size, yi % size, (yi + 1) % size
    a = g[y0, x0] * (1 - u) + g[y0, x1] * u
    b = g[y1, x0] * (1 - u) + g[y1, x1] * u
    return a * (1 - v) + b * v


def fbm2d(x, y, seed, octaves=5):
    total, amp, freq, norm = np.zeros_like(x, dtype=np.float64), 1.0, 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise_2d(x * freq, y * freq, seed + o * 37)
        norm += amp
        amp *= 0.5
        freq *= 2.03
    return total / norm


def hexc(h):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float64) / 255.0


def mix(a, b, t):
    t = np.asarray(t)[..., None] if np.ndim(t) else t
    return a * (1 - t) + b * t


# ----------------------------------------------------------------------------- scene
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)
ny, nx = yy / H, xx / W

# Sky: deep teal at the top, pale cold grey at the horizon, a hint of warm light low on the left.
sky_top, sky_mid, sky_hor = hexc("#0a1417"), hexc("#203236"), hexc("#7f8d87")
t = np.clip(ny / 0.62, 0, 1)
img = np.where((t < 0.55)[..., None], mix(sky_top, sky_mid, t / 0.55), mix(sky_mid, sky_hor, (t - 0.55) / 0.45))
warm = np.exp(-(((nx - 0.18) / 0.35) ** 2 + ((ny - 0.58) / 0.16) ** 2))
img = mix(img, hexc("#b39a77"), 0.22 * warm)

# Clouds: stretched fbm, brighter at the top layer, soft.
c = fbm2d(xx / 260.0, yy / 90.0, 11, 6)
c = np.clip((c - 0.48) * 2.6, 0, 1) * np.clip(1.2 - ny * 1.6, 0, 1)
img = mix(img, hexc("#9aa7a2"), 0.35 * c)
c2 = fbm2d(xx / 120.0 + 40, yy / 45.0, 23, 5)
img = mix(img, hexc("#0e1a1d"), 0.35 * np.clip((c2 - 0.55) * 3, 0, 1) * np.clip(0.9 - ny * 1.2, 0, 1))

# Faint inverted rainbow (a smile hanging in the sky).
cx, cy, r0 = 0.62 * W, -0.30 * H, 0.78 * H
d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
band = (d - r0) / (0.055 * H)
rb_cols = [hexc(h) for h in ("#d65c5c", "#d9a35a", "#d8d36a", "#6fbf7a", "#5d8fd1", "#8a6cc4")]
for i, col in enumerate(rb_cols):
    w = np.exp(-((band - (i / 5.0)) / 0.16) ** 2) * np.clip(1.0 - np.abs(nx - 0.62) * 2.2, 0, 1)
    img = mix(img, col, 0.10 * w * (ny < 0.6))

# Ridges back -> front: (base y, amplitude, frequency, colour, fog amount, seed)
ridges = [
    (0.50, 0.17, 1.6, "#4f5d5b", 0.66, 3),
    (0.56, 0.15, 2.0, "#3a4846", 0.52, 5),
    (0.63, 0.12, 2.6, "#2a3631", 0.38, 7),
    (0.71, 0.09, 3.0, "#1a231f", 0.20, 9),
    (0.79, 0.05, 2.2, "#0d1311", 0.05, 13),
]
fog = hexc("#7d8b86")
ridge_y = []
xs = np.arange(W, dtype=np.float64)


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


for i, (base, amp, freq, col, fogamt, seed) in enumerate(ridges):
    u = xs / W * freq * 2.5 + seed * 10
    big = fbm1d(u, seed, 3, 2.0, 0.5)                                # large silhouettes
    ridged = 1.0 - np.abs(2.0 * fbm1d(u * 2.3, seed + 50, 5, 2.1, 0.45) - 1.0)  # sharp crests
    fine = fbm1d(u * 9.0, seed + 90, 4, 2.0, 0.5)                    # rocky detail
    envelope = 0.55 + 0.9 * fbm1d(u * 0.35, seed + 7, 2)            # some ranges higher than others
    h = (0.62 * big + 0.30 * ridged + 0.08 * fine) * envelope
    if i == len(ridges) - 1:
        # Foreground: a smooth hill on the right where the porters walk.
        hill = np.exp(-((xs / W - 0.74) / 0.20) ** 2)
        y = (base - amp * (h - 0.5) - 0.085 * hill) * H
    elif i == len(ridges) - 2:
        y = (base - amp * h) * H
        # Backlight in the mist behind the foreground hill: silhouettes read against it.
        glow = np.exp(-(((nx - 0.74) / 0.22) ** 2 + ((ny - 0.62) / 0.12) ** 2))
        img = mix(img, hexc("#c4c0aa"), 0.45 * glow)
    else:
        y = (base - amp * h) * H
    ridge_y.append(y)
    layer_col = mix(hexc(col), fog, fogamt)
    rock = fbm2d(xx / (40 + 30 * i), yy / (18 + 10 * i), 100 + i, 5)
    depth = np.clip((yy - y[None, :]) / (0.25 * H), 0, 1)
    contrast = 0.30 if i < 4 else 0.12
    shade = layer_col * (1.0 - contrast / 2 + contrast * rock[..., None]) * (1.0 - 0.25 * depth[..., None])
    moss = hexc("#3d4b3c") * (0.8 + 0.3 * rock[..., None])
    shade = mix(shade, moss, 0.16 * (1 - fogamt) * smoothstep(0.50, 0.68, rock))
    mask = yy >= y[None, :]
    edge = np.clip(yy - y[None, :] + 0.5, 0, 1)
    img = np.where(mask[..., None], mix(img, shade, edge), img)
    mist = fbm2d(xx / 200.0 + i * 13, yy / 30.0, 200 + i, 4)
    band_y = (base + 0.04) * H
    mistw = np.exp(-((yy - band_y) / (0.05 * H)) ** 2) * np.clip((mist - 0.35) * 2.2, 0, 1)
    img = mix(img, fog, 0.30 * mistw * (i < 4))

front = ridge_y[-1]


def ridge_at(x):
    return float(front[int(np.clip(x, 0, W - 1))])


# ----------------------------------------------------------------------------- porters
SS = 4  # supersampling for clean silhouettes
sil = Image.new("L", (W * SS, H * SS), 0)
glow_pts = []
draw = ImageDraw.Draw(sil)


def porter(x, scale, stride):
    """Side view, walking right, big cargo frame on the back (original design)."""
    gy = ridge_at(x) + 1.5
    s = scale * SS
    X, Y = x * SS, gy * SS

    def P(px, py):
        return (X + px * s, Y - py * s)

    def poly(pts):
        draw.polygon([P(a, b) for a, b in pts], fill=255)

    # legs: back leg and front leg in stride
    poly([(-2.6, 0), (0.6, 0), (2.4, 15), (-0.8, 16)])
    poly([(3.0 + stride, 0), (6.2 + stride, 0), (3.6, 16), (0.4, 15)])
    poly([(-3.4, 0), (1.0, 0), (0.8, 2.0), (-3.2, 2.2)])
    poly([(2.6 + stride, 0), (7.4 + stride, 0), (6.8 + stride, 2.2), (2.8 + stride, 2.2)])
    # hips + torso leaning forward
    poly([(-1.6, 14.5), (4.8, 14.5), (6.6, 29.5), (0.6, 30.5)])
    # arm holding the shoulder strap
    poly([(4.8, 28.8), (7.0, 28.2), (8.2, 21.0), (6.4, 20.4)])
    # head + hood
    hx0, hy0 = P(2.6, 37.4)
    hx1, hy1 = P(7.8, 31.6)
    draw.ellipse([hx0, hy0, hx1, hy1], fill=255)
    poly([(1.6, 36.0), (4.6, 38.2), (7.4, 36.2), (4.0, 31.0)])
    # cargo frame: wide, taller than the porter's head
    poly([(-9.6, 12.5), (0.8, 13.5), (1.6, 35.0), (-8.8, 35.5)])
    poly([(-10.6, 35.0), (0.8, 34.6), (1.2, 44.0), (-10.2, 44.4)])
    poly([(-9.0, 44.0), (-0.2, 43.8), (0.2, 50.5), (-8.6, 50.8)])
    # straps sticking out a little
    poly([(-10.8, 23.0), (-9.4, 23.0), (-9.4, 25.0), (-10.8, 25.0)])
    glow_pts.append((x + 6.6 * scale, gy - 25.5 * scale, scale))


porter(0.700 * W, 1.30, 1.2)
porter(0.765 * W, 1.42, -0.4)
sil = sil.resize((W, H), Image.LANCZOS)
sil_a = np.asarray(sil, dtype=np.float64) / 255.0
img = mix(img, hexc("#070b0a"), sil_a)

# Rim light on the silhouettes from the warm horizon (thin, subtle).
rim = np.asarray(sil.filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float64) / 255.0
rim = np.clip(rim - sil_a, 0, 1)
img = mix(img, hexc("#9b8a6c"), 0.35 * rim)

# Pod lights: tiny amber glows.
for gx, gy, sc in glow_pts:
    d2 = (xx - gx) ** 2 + (yy - gy) ** 2
    img = img + hexc("#ffb347") * (0.9 * np.exp(-d2 / (2 * (1.2 * sc) ** 2)) + 0.25 * np.exp(-d2 / (2 * (5 * sc) ** 2)))[..., None]

# ----------------------------------------------------------------------------- finishing
# Left side darker so the UI panel reads well.
img = img * (0.62 + 0.38 * np.clip((nx - 0.05) / 0.5, 0, 1))[..., None]
# Vignette.
vig = 1.0 - 0.55 * np.clip(((nx - 0.55) / 0.75) ** 2 + ((ny - 0.5) / 0.8) ** 2, 0, 1)
img = img * vig[..., None]
# Film grain.
img = img + (rng.standard_normal((H, W, 1)) * 0.012)
img = np.clip(img, 0, 1)

os.makedirs(OUT, exist_ok=True)
Image.fromarray((img * 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "hero.jpg"), quality=90, optimize=True, progressive=True)

# Network nodes in window design units (920x580): on ridge tops, used by the C++ animation.
node_x = [0.47, 0.56, 0.700, 0.765, 0.88, 0.95]
with open(os.path.join(OUT, "nodes.txt"), "w") as f:
    for i, fx in enumerate(node_x):
        px = fx * W
        if fx in (0.700, 0.765):
            sc = 1.30 if fx == 0.700 else 1.42
            py = ridge_at(px) - 25.5 * sc       # pod light on the chest
            px += 6.6 * sc
        else:
            py = float(ridge_y[2 + (i % 2)][int(px)]) - 2
        f.write(f"{px / DESIGN_SCALE:.1f} {py / DESIGN_SCALE:.1f}\n")


# ----------------------------------------------------------------------------- icon
def make_icon(size):
    s = size * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad = s * 0.04
    d.ellipse([pad, pad, s - pad, s - pad], fill=(12, 21, 24, 255))
    # golden ring
    for k, a in ((0.115, 90), (0.085, 255)):
        w = s * k
        d.ellipse([pad + w * 0.6, pad + w * 0.6, s - pad - w * 0.6, s - pad - w * 0.6], outline=(240, 190, 90, a), width=int(w * 0.55))
    # chiral strand: two interleaved arcs through the centre
    c = s / 2
    pts1 = [(c + math.cos(t) * s * 0.28, c + math.sin(2 * t) * s * 0.12) for t in np.linspace(-math.pi, math.pi, 80)]
    d.line(pts1, fill=(255, 214, 128, 255), width=max(2, int(s * 0.045)), joint="curve")
    d.ellipse([c - s * 0.07, c - s * 0.07, c + s * 0.07, c + s * 0.07], fill=(255, 236, 190, 255))
    glow = im.filter(ImageFilter.GaussianBlur(s * 0.02))
    out = Image.alpha_composite(glow, im)
    return out.resize((size, size), Image.LANCZOS)


icon = make_icon(256)
icon.save(os.path.join(OUT, "icon.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
icon.save(os.path.join(OUT, "icon.png"))
print("hero.jpg, icon.ico, nodes.txt written to", OUT)
print(open(os.path.join(OUT, "nodes.txt")).read())
