"""Convert every .ppm in a directory to .png (and delete the .ppm)."""
import os
import sys

from PIL import Image

d = sys.argv[1]
for name in sorted(os.listdir(d)):
    if name.endswith(".ppm"):
        Image.open(os.path.join(d, name)).save(os.path.join(d, name[:-4] + ".png"))
        os.remove(os.path.join(d, name))
print("PNGs in", d)
