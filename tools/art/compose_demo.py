#!/usr/bin/env python3
"""Put recorded installer frames (DSCoopSetup.exe /preview=DIR) on a desktop-like background with
rounded corners and a soft shadow, the way the window looks on Windows 11.

Usage: compose_demo.py FRAMES_DIR OUT_DIR
Writes OUT_DIR/f_NNNN.png (1280x800). Encode with ffmpeg (see scripts/record-installer-demo.sh).
"""
import glob
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
CW, CH = 1280, 800
frames = sorted(glob.glob(os.path.join(src, "frame_*.png")))
first = Image.open(frames[0])
WW, WH = first.size
X, Y = (CW - WW) // 2, (CH - WH) // 2 - 10

# Desktop wallpaper: deep blue-teal gradient with soft colour blooms (neutral, no branding).
yy, xx = np.mgrid[0:CH, 0:CW].astype(np.float64)
base = np.zeros((CH, CW, 3))
top, bot = np.array([18, 30, 44]) / 255, np.array([8, 12, 20]) / 255
t = (yy / CH)[..., None]
base = top * (1 - t) + bot * t
for cx, cy, r, col, a in ((0.22, 0.25, 0.45, (40, 110, 140), 0.55), (0.82, 0.75, 0.5, (150, 110, 60), 0.35), (0.65, 0.15, 0.35, (70, 60, 130), 0.3)):
    d = ((xx / CW - cx) ** 2 + (yy / CH - cy) ** 2) / r ** 2
    base = base + (np.array(col) / 255) * a * np.exp(-d * 2.2)[..., None]
wall = Image.fromarray((np.clip(base, 0, 1) * 255).astype(np.uint8), "RGB")

radius = 9
mask = Image.new("L", (WW, WH), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, WW - 1, WH - 1], radius=radius, fill=255)
shadow = Image.new("L", (CW, CH), 0)
ImageDraw.Draw(shadow).rounded_rectangle([X - 2, Y + 10, X + WW + 2, Y + WH + 18], radius=radius + 6, fill=150)
shadow = shadow.filter(ImageFilter.GaussianBlur(26))
desk = wall.copy()
desk.paste(Image.new("RGB", (CW, CH), (0, 0, 0)), (0, 0), shadow)

fade_frames = 14  # the window fades in on open (layered window alpha is not in the recorded bitmap)
for i, path in enumerate(frames):
    win = Image.open(path).convert("RGB")
    canvas = wall.copy() if i < fade_frames else desk.copy()
    if i < fade_frames:
        k = (i + 1) / fade_frames
        k = 1 - (1 - k) ** 3
        sh = shadow.point(lambda v, k=k: int(v * k))
        canvas.paste(Image.new("RGB", (CW, CH), (0, 0, 0)), (0, 0), sh)
        m = mask.point(lambda v, k=k: int(v * k))
    else:
        m = mask
    canvas.paste(win, (X, Y), m)
    # 1px light border like Windows 11 windows
    ImageDraw.Draw(canvas).rounded_rectangle([X, Y, X + WW - 1, Y + WH - 1], radius=radius, outline=(70, 80, 84))
    canvas.save(os.path.join(out, f"f_{i:04d}.png"))
print(f"{len(frames)} frames -> {out}")
