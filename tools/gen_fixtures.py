#!/usr/bin/env python3
# Optional development tool (needs msi-perkeyrgb and msi-kbd-effect installed locally).
# Records the packets the user's validated tools (msi-perkeyrgb and msi-kbd-effect) would
# send, without opening any device: their HID class is replaced by a recorder. The C++
# protocol is then compared byte for byte against these files.
#
# Run with: python3 -I tools/gen_fixtures.py

import builtins
import importlib.machinery
import importlib.util
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURES = os.path.join(ROOT, "tests", "fixtures")
MSI_PERKEYRGB = os.path.expanduser("~/.local/share/msi-perkeyrgb")
MSI_KBD_EFFECT = os.path.expanduser("~/.local/share/msi-perkeyrgb-setup/msi-kbd-effect")

sys.path.insert(0, MSI_PERKEYRGB)

# Refuse any attempt to touch a device node while recording.
_real_open = builtins.open


def _guarded_open(file, *args, **kwargs):
    if str(file).startswith("/dev"):
        raise RuntimeError("fixture generation must never open %s" % file)
    return _real_open(file, *args, **kwargs)


builtins.open = _guarded_open


class Recorder:
    def __init__(self, *args, **kwargs):
        self.packets = []

    def send_feature_report(self, data):
        self.packets.append(("F", bytes(data)))

    def send_output_report(self, data):
        self.packets.append(("O", bytes(data)))


import msi_perkeyrgb.msi_keyboard as msi_keyboard  # noqa: E402
from msi_perkeyrgb.msiprotocol import make_key_colors_packet, make_refresh_packet  # noqa: E402
from msi_perkeyrgb.protocol_data.keycodes import REGION_KEYCODES  # noqa: E402

msi_keyboard.HID_Keyboard = Recorder


def write(name, packets):
    with _real_open(os.path.join(FIXTURES, name), "w") as f:
        for kind, data in packets:
            f.write("%s %s\n" % (kind, data.hex()))


def keyboard():
    keymap = msi_keyboard.MSI_Keyboard.get_model_keymap("GS75")
    presets = msi_keyboard.MSI_Keyboard.get_model_presets("GS75")
    return msi_keyboard.MSI_Keyboard((0x1038, 0x113A), keymap, presets)


def static_all():
    kb = keyboard()
    kb.set_color_all([0x00, 0xE5, 0xFF])
    kb.refresh()
    return kb._hid_keyboard.packets


def static_zqsd():
    # Physical A Z E Q S D on AZERTY are the QWERTY Q W E A S D positions.
    red_hids = {0x14, 0x1A, 0x08, 0x04, 0x16, 0x07}
    packets = []
    for region, codes in REGION_KEYCODES.items():
        colors = {code: ([0xFF, 0x00, 0x00] if code in red_hids else [0x00, 0xE5, 0xFF]) for code in codes}
        packets.append(("F", bytes(make_key_colors_packet(region, colors))))
    packets.append(("O", bytes(make_refresh_packet())))
    return packets


def effect(name):
    loader = importlib.machinery.SourceFileLoader("msi_kbd_effect", MSI_KBD_EFFECT)
    module = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
    loader.exec_module(module)
    recorder = Recorder()
    module.HID_Keyboard = lambda *args: recorder
    sys.argv = ["msi-kbd-effect", name]
    module.main()
    return recorder.packets


def main():
    os.makedirs(FIXTURES, exist_ok=True)
    write("static-00e5ff.hex", static_all())
    write("static-zqsd.hex", static_zqsd())
    write("ares.hex", effect("ares"))
    write("tron.hex", effect("tron"))

    with _real_open(os.path.join(FIXTURES, "regions.json"), "w") as f:
        json.dump({region: list(codes) for region, codes in REGION_KEYCODES.items()}, f, indent=1)

    with _real_open(os.path.join(MSI_PERKEYRGB, "msi_perkeyrgb/protocol_data/presets/1.json")) as f:
        aqua = json.load(f)["aqua"]
    for path in (os.path.join(FIXTURES, "aqua.json"), os.path.join(ROOT, "src", "presets", "aqua.json")):
        with _real_open(path, "w") as f:
            json.dump(aqua, f, indent=1)


if __name__ == "__main__":
    main()
