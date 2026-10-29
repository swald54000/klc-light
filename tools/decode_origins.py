#!/usr/bin/env python3
# Prints the wave parameters of the captured aqua effect packets (bytes 0x8A..0x9B), the
# part msi-kbd-effect keeps as-is, decoded under the field mapping proposed in
# docs/origins.md. Offline only: reads tests/fixtures/aqua.json.
#
# Run with: python3 -I tools/decode_origins.py

import json
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

packets = [bytes.fromhex(h) for h in json.load(open(os.path.join(ROOT, "tests", "fixtures", "aqua.json")))]
print("id  0x8A  focal_x  focal_y  0x90  0x92  0x94  count  total  0x9A")
for packet in packets:
    if packet[0] != 0x0B:
        continue
    level, x, y, wave, pulse, scale = struct.unpack_from("<HhhHHH", packet, 0x8A)
    count, total, inverted = packet[0x96], struct.unpack_from("<H", packet, 0x98)[0], packet[0x9A]
    print("%2d  %4d  %7d  %7d  %4d  %4d  %4d  %5d  %5d  %4d" % (packet[2], level, x, y, wave, pulse, scale, count, total, inverted))
