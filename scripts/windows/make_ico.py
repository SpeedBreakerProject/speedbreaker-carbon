#!/usr/bin/env python3
# make_ico.py: SpeedBreaker's Windows icon (runtime/platform/win32/
# speedbreaker.ico, kept in the repo so the build needs no Pillow) from the
# macOS one, scripts/macos/AppIcon.png. macOS draws its icons inside a
# margin (an 824-pixel rounded square in 1024); Windows icons fill their
# square, so the margin is cropped first. Needs Pillow. --carbon makes NFS:
# Carbon's, runtime/platform/win32/speedbreaker-carbon.ico, from
# scripts/macos/AppIcon-carbon.png (speedbreaker.rc picks it in a Carbon build).
#   python3 scripts/windows/make_ico.py [--carbon]
import os
import sys
from PIL import Image

CARBON = '--carbon' in sys.argv[1:]

root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
src = Image.open(os.path.join(root, 'scripts/macos/AppIcon-carbon.png' if CARBON else 'scripts/macos/AppIcon.png')).convert('RGBA')
art = src.crop((96, 96, 928, 928)).resize((256, 256), Image.LANCZOS)
out = os.path.join(root, 'runtime/platform/win32/speedbreaker-carbon.ico' if CARBON else 'runtime/platform/win32/speedbreaker.ico')
art.save(out, format='ICO', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(out, os.path.getsize(out), 'bytes')
