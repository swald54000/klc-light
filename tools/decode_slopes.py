#!/usr/bin/env python3
# Decodes the effect packets of the captured "aqua" preset and checks which slope model
# closes each color cycle. Offline: reads src/presets/aqua.json, opens no device.
#
#   ours    : each transition adds 2 * slope color levels, whatever its duration
#   keyglow : each transition adds slope * duration / 16 color levels
#
# Run with: python3 -I tools/decode_slopes.py

import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

with open(os.path.join(ROOT, "src", "presets", "aqua.json")) as f:
    packets = [bytes.fromhex(h) for h in json.load(f)]

for p in packets:
    if p[0] != 0x0B:
        continue
    count = p[0x96]
    start = [struct.unpack_from("<H", p, 0x84 + 2 * c)[0] / 16 for c in range(3)]
    table = []
    for i in range(count):
        o = 2 + i * 8
        table.append((struct.unpack_from("<bbb", p, o + 2), struct.unpack_from("<H", p, o + 6)[0]))
    x, y, ax, ay, length = struct.unpack_from("<hHHHH", p, 0x8C)
    print(f"effect {p[2]}: start {start}, x={x} y={y} along_x={ax} along_y={ay} "
          f"wavelength={length} inward={p[0x9A]}")
    color_ours, color_kg = list(start), list(start)
    for slope, duration in table:
        color_ours = [c + 2 * s for c, s in zip(color_ours, slope)]
        color_kg = [c + s * duration / 16 for c, s in zip(color_kg, slope)]
        print(f"  slope {slope} x {duration:3}  ours -> {[round(c) for c in color_ours]}"
              f"  keyglow -> {[round(c) for c in color_kg]}")
