# Tileable water ripple normal (DirectX), from a sum of integer-frequency waves.
import math, random, sys
import numpy as np
from PIL import Image
random.seed(3)
S = 1024
y, x = np.mgrid[0:S, 0:S] / S
dhdx = np.zeros((S, S)); dhdy = np.zeros((S, S))
for _ in range(48):
    k = random.choice([2, 3, 3, 4, 5, 6, 7, 8, 9, 11, 13, 16])
    th = random.uniform(0, 2 * math.pi)
    kx, ky = round(k * math.cos(th)), round(k * math.sin(th))
    if kx == 0 and ky == 0:
        continue
    a = 1.0 / (math.hypot(kx, ky) ** 1.4)
    ph = random.uniform(0, 2 * math.pi)
    arg = 2 * math.pi * (kx * x + ky * y) + ph
    # derivative of a*sin(arg)
    dhdx += a * 2 * math.pi * kx * np.cos(arg)
    dhdy += a * 2 * math.pi * ky * np.cos(arg)
strength = 0.012
nx, ny, nz = -dhdx * strength, -dhdy * strength, np.ones((S, S))
l = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
n = np.dstack([nx / l, -(ny / l), nz / l])   # DirectX: flip green
Image.fromarray(((n + 1) / 2 * 255 + 0.5).astype(np.uint8)).save((sys.argv[1] if len(sys.argv) > 1 else ".") + "/T_Water_N.png")
print("slope max", float(np.abs(nx).max()), float(np.abs(ny).max()))
