#!/usr/bin/env python3
# Generates src/layoutdata.cpp, the GS76 key table: key rectangles measured on a photo of
# the keyboard (below), HID codes and regions from msi-perkeyrgb, Linux key names from the
# kernel headers.
#
# Run with: python3 -I tools/gen_layout.py

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MSI_PERKEYRGB = os.path.expanduser("~/.local/share/msi-perkeyrgb")
INPUT_CODES = "/usr/include/linux/input-event-codes.h"
# SteelSeries GG's GE76 KLC driver (the GS76 includes it), decrypted: key positions in
# GG's units, which scale to the controller's own wave coordinates.
GG_LAYOUT = os.path.expanduser("~/Work/klc-re/decrypted/all/msi-ge76-klc.lisp")

sys.path.insert(0, MSI_PERKEYRGB)
from msi_perkeyrgb.protocol_data.msi_keymaps import AVAILABLE_MSI_KEYMAPS  # noqa: E402

REGIONS = ["alphanum", "enter", "modifiers", "numpad"]
REGION_ENUM = {"alphanum": "Alphanum", "enter": "Enter", "modifiers": "Modifiers", "numpad": "Numpad"}
AQUA_PREFIX = {"alphanum": 42, "enter": 11, "modifiers": 24, "numpad": 36}

# Key rectangles measured on a top-down photo of the GS76 replacement keyboard (1000 px
# wide, y downwards), by HID code: (left, right, top, bottom). The French GS76 uses the
# same matrix: "* µ" is HID 49 above the one-row Enter, "< >" is HID 100 right of AltGr.
# The photo itself is a measurement reference and is not part of the project.
ROWS = {"f": (183, 212), "n": (223, 267), "q": (278, 322), "a": (332, 375), "z": (387, 430), "b": (442, 487)}
PHOTO = {}
def row(name, *keys):
    top, bottom = ROWS[name]
    for hid, left, right in keys:
        PHOTO[hid] = (left, right, top, bottom)
row("f", (41, 5, 42), (58, 57, 97), (59, 107, 147), (60, 157, 197), (61, 205, 245), (62, 258, 297),
    (63, 307, 345), (64, 355, 395), (65, 405, 445), (66, 455, 495), (67, 507, 545), (68, 557, 595),
    (69, 605, 643), (71, 660, 697), (73, 710, 748), (76, 760, 798), (70, 820, 853), (75, 867, 902), (78, 915, 950))
row("n", (53, 5, 70), (30, 82, 122), (31, 133, 173), (32, 187, 227), (33, 238, 278), (34, 290, 330),
    (35, 343, 383), (36, 395, 435), (37, 447, 487), (38, 500, 541), (39, 553, 593), (45, 605, 647),
    (46, 658, 700), (42, 712, 797), (83, 820, 853), (84, 867, 902), (85, 915, 950), (86, 963, 997))
row("q", (43, 5, 82), (20, 92, 132), (26, 144, 185), (8, 196, 237), (21, 249, 289), (23, 301, 341),
    (28, 353, 394), (24, 406, 446), (12, 458, 499), (18, 510, 550), (19, 563, 602), (47, 617, 657),
    (48, 668, 708), (49, 722, 798), (95, 820, 853), (96, 867, 902), (97, 915, 950))
row("a", (57, 5, 97), (4, 108, 148), (22, 160, 200), (7, 212, 253), (9, 264, 305), (10, 317, 357),
    (11, 369, 409), (13, 421, 461), (14, 473, 513), (15, 525, 567), (51, 578, 618), (52, 632, 672),
    (40, 685, 798), (92, 820, 853), (93, 867, 902), (94, 915, 950))
row("z", (225, 5, 125), (29, 136, 176), (27, 188, 228), (6, 240, 281), (25, 292, 333), (5, 345, 385),
    (17, 397, 437), (16, 450, 490), (54, 500, 543), (55, 553, 597), (56, 605, 648), (229, 658, 743),
    (82, 755, 797), (89, 820, 853), (90, 867, 902), (91, 915, 950))
row("b", (224, 5, 95), (227, 103, 148), (226, 157, 208), (44, 220, 487), (230, 492, 535), (100, 547, 585),
    (240, 598, 637), (228, 650, 688), (80, 703, 740), (81, 757, 797), (79, 812, 850), (98, 867, 898), (99, 915, 948))
# The two tall keypad keys span two rows.
PHOTO[87] = (963, 997, 278, 375)
PHOTO[88] = (963, 997, 387, 487)

# The power key, top right above the keypad: in none of msi-perkeyrgb's regions, it is
# sent as a twelfth element of the 11-key packet (protocol.cpp).
PHOTO[102] = (963, 997, 183, 212)
POWER_REGION = "enter"
EXCLUDED = set()
# Linux maps both "non-US" codes like this (drivers/hid/hid-input.c), and HID 0x66 to
# KEY_POWER.
EXTRA_EVDEV = {50: 43, 100: 86, 102: 116}


def gg_positions():
    """HID code -> (x, y) in controller units: GG's fixed positions, then the English
    variant (layout 0) for the keys GG places by layout; GG has no French variant."""
    text = open(GG_LAYOUT).read()

    def triples(body):
        return {int(h): (int(x), int(y)) for h, x, y in re.findall(r"\((\d+) (\d+) (\d+)\)", body)}

    fixed = text.split("(define (get-prism-sync-nonvariant-hid-info)")[1].split("(define")[0]
    variant = text.split("(define (get-prism-sync-variant-hid-info)")[1].split("((3)")[0]
    positions = triples(variant)
    positions.update(triples(fixed))
    # GG: x 140..807, y 89..256 growing upward; the controller spans x 0..4294 with y
    # growing downward (checked on the GS76: a wave "toward y 1075" ran down to the space bar).
    scale = 4294 / (807 - 140)
    return {h: (round((x - 140) * scale), round((256 - y) * scale)) for h, (x, y) in positions.items()}


def main():
    keymap = next(m for models, m in AVAILABLE_MSI_KEYMAPS if "GS75" in models)
    # msi-perkeyrgb keys its map by X11 keycode (evdev + 8), except Fn, which has no
    # keycode at all: the controller handles it internally.
    hid_to_evdev = {hid: x11 - 8 for x11, hid in keymap.items() if isinstance(x11, int)}
    hid_to_evdev.update(EXTRA_EVDEV)

    names = {}
    for line in open(INPUT_CODES):
        match = re.match(r"#define (KEY_\w+)\s+(\d+)\b", line)
        if match and int(match.group(2)) not in names:
            names[int(match.group(2))] = match.group(1)

    regions = json.load(open(os.path.join(ROOT, "tests", "fixtures", "regions.json")))

    gg = gg_positions()
    keys = []
    for hid, (left, right, top, bottom) in sorted(PHOTO.items()):
        region = POWER_REGION if hid == 102 else next(r for r in REGIONS if hid in regions[r])
        name = "KEY_FN" if hid == 240 else names[hid_to_evdev[hid]]
        if hid not in gg:
            sys.exit("no GG position for visible key %s (HID %d)" % (name, hid))
        keys.append((name, hid, REGION_ENUM[region], (left + right) / 2, (top + bottom) / 2, right - left, bottom - top) + gg[hid])

    # Codes the captured aqua preset lights but GG has no position for (Pause, Home, End,
    # Menu, JIS and Korean keys): kept so the preset's key mapping can be reproduced,
    # never drawn.
    placed = {k[1] for k in keys}
    for region in REGIONS:
        for hid in regions[region][:AQUA_PREFIX[region]]:
            if hid in placed or hid in EXCLUDED:
                continue
            evdev = hid_to_evdev.get(hid)
            name = names.get(evdev) if evdev is not None else None
            # Ids must be unique: HID 50 shares KEY_BACKSLASH with the visible HID 49.
            if name in {k[0] for k in keys}:
                name = None
            keys.append((name or "HID_%d" % hid, hid, REGION_ENUM[region], 0, 0, 0, 0, -1, -1))
            placed.add(hid)

    out = [
        "// Generated by tools/gen_layout.py from key rectangles measured on a GS76 keyboard photo, msi-perkeyrgb,",
        "// SteelSeries GG's GE76 key positions and linux/input-event-codes.h. Regenerate rather than edit by hand.",
        "",
        '#include "layoutdata.h"',
        "",
        "const KeyData kKeyData[] = {",
    ]
    out += ['    {"%s", %d, Region::%s, %.1f, %.1f, %d, %d, %s, %d, %d},' % (k[:7] + ("true" if k[5] > 0 else "false",) + k[7:]) for k in keys]
    out += ["};", "const int kKeyDataCount = %d;" % len(keys), ""]
    out.append("const quint8 kRegionHids[4][42] = {")
    for region in REGIONS:
        out.append("    {%s}," % ", ".join(str(h) for h in regions[region]))
    out += ["};", ""]
    out.append("const int kAquaPrefix[4] = {%s};" % ", ".join(str(AQUA_PREFIX[r]) for r in REGIONS))

    with open(os.path.join(ROOT, "src", "layoutdata.cpp"), "w") as f:
        f.write("\n".join(out) + "\n")
    print("%d keys" % len(keys))


if __name__ == "__main__":
    main()
