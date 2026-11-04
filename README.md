# KLC Light

A per-key lighting editor for the keyboard of MSI laptops with a SteelSeries KLC
controller (USB `1038:113a`), made for [Omarchy](https://omarchy.org) on Arch Linux.

- A library of named lighting profiles; each can be linked to an Omarchy theme, so a
  theme switch changes the keyboard too.
- Key groups, each with its own steady color, pulse or wave; up to 18 animated groups,
  all run by the keyboard's own controller (nothing keeps running on the computer).
- Waves in eight directions, or rippling toward or away from any key; reactive keys that
  light up when pressed.
- An animated on-screen preview, undo and redo, and nothing sent until you press Apply.
- A command line for scripts and hooks, and a bar widget for Omarchy's shell.

## Supported hardware

Tested on an **MSI GS76 Stealth** (French AZERTY keyboard). Other MSI laptops whose
keyboard lighting controller is `1038:113a` (check with `lsusb | grep 1038:113a`) may
work; key positions follow SteelSeries GG's GE76 family. Please open an issue with your
model, whether it works or not.

## Install

```sh
yay -S klc-light
sudo usermod -aG klc-light $USER
```

Then log out and back in. The package adds a `klc-light` group and a udev rule that lets
its members attach and detach the lighting controller (through the controller's own sysfs
`authorized` file; nothing runs as root). To try the editor without touching the
keyboard, start it with `KLC_LIGHT_FAKE_DEVICE=1 klc-light`: packets are printed instead
of sent.

### Omarchy hooks

To change the lighting with the theme and restore it at boot, add two hooks. Omarchy waits
for its hooks, so they hand the send to the background (a send can wait a few seconds for
the previous one). A status of 2 means the controller dropped off the USB bus, which only a
full power-off clears.

`~/.config/omarchy/hooks/theme-set.d/klc-light`:

```sh
#!/bin/sh
setsid --fork bash -c 'set -o pipefail
  klc-light apply --theme 2>&1 | systemd-cat -t klc-light
  [ $? -eq 2 ] && notify-send -u critical -a "KLC Light" "Keyboard lighting controller lost (USB error -71)" \
    "Shut the computer down completely, then start it again: a restart is not enough."' </dev/null >/dev/null 2>&1
```

`~/.config/omarchy/hooks/post-boot.d/klc-light`:

```sh
#!/bin/sh
setsid --fork bash -c 'set -o pipefail
  klc-light apply 2>&1 | systemd-cat -t klc-light
  [ $? -eq 2 ] && notify-send -u critical -a "KLC Light" "Keyboard lighting controller lost (USB error -71)" \
    "Shut the computer down completely, then start it again: a restart is not enough."' </dev/null >/dev/null 2>&1
```

A theme without a linked profile leaves the keyboard alone; messages go to the user journal
(`journalctl --user -t klc-light`).

### Bar widget

```sh
omarchy plugin add https://github.com/swald54000/omarchy-klc-light.git --enable
```

A keyboard icon (dimmed while the lighting is off) whose tooltip names the applied
profile. Click it for the profile list (a click applies one), right-click to turn the
keyboard off, middle-click to turn it back on; the panel also opens the editor. It
follows what the editor and the hooks apply.

## Use

`klc-light` opens the editor on the profile last applied. The bar at the top creates,
duplicates, renames and deletes profiles and links the open one to a theme.

| Shortcut | Action |
|---|---|
| Space | Apply to the keyboard |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Esc | Clear the selection |
| ? | Show the shortcuts |
| Q | Quit (asks if changes are unapplied) |

### Command line

- `klc-light apply` sends the last applied profile; `--theme` sends the one linked to the
  active Omarchy theme; `klc-light apply <name>` a library profile;
  `klc-light apply <file.json>` a profile file.
- `klc-light off` turns every key off and remembers it; the applied profile is kept, and
  any later `apply` (a theme switch included) turns it back on.
- `klc-light list` prints the profiles, `*` marking the applied one; `--json` adds each
  one's theme link, unreadable files and the active theme.
- `--check` builds and validates without sending (`N packets OK`); `--json` gives
  machine-readable output, for sends too.
- Exit status: 0 on success or when there is nothing to apply, 1 on any error (message on
  stderr), 2 when the controller is absent from the USB bus (on the GS76 that happens after
  a warm restart; only a full power-off brings it back).

### Waves

The base and each wave group run toward or away from an origin: the left or right edge,
the top or bottom, the centre, or a key picked on the keyboard (Pick the key, then click
it; Esc cancels). The size sets how fast the wave travels, from 1 (narrow bands) to 100.
In a profile file:

```json
"wave": { "origin": "KEY_G", "flow": "away", "size": 40 }
```

### Reactive keys

The REACTIVE row lights keys when pressed: the pressed color lasts 50 to 2000 ms (300 by
default), then fades back.

### Profile files

Profiles are plain files in `~/.config/klc-light/profiles/<name>.klc.json`;
`~/.config/klc-light/state.json` records the applied profile, whether the lighting is off
and the theme links (a corrupt one is moved aside as `state.json.bad`). On first run, the
old per-theme files (`~/.config/omarchy/themes/<theme>/keyboard.klc.json`, else
`keyboard.effect`, else `keyboard.rgb`) become profiles linked to their theme; with
nothing to migrate a `Default` profile is created. Older formats are converted when read
and written in the current one (version 3) at the next save.

## Known limits

- **Power key:** its LED shows F1's color whatever is sent, so the editor draws it in F1's
  color and a click on it picks F1. The small orange light beside it is a separate
  indicator the controller does not drive.
- **LED-matrix ghosting:** a lit key leaks a faint glow, mostly green, into some
  neighbours (pressing AZERTY Q also glows A). Colors with little green avoid it.
- **One send at a time:** the controller hangs on repeated sends, so each Apply sends once
  and sends are spaced by a few seconds.
- 18 animated groups at most (the controller's effect slots); 16 steps per cycle.

## How it works

The controller takes 524-byte HID feature reports: key tables (`0x0e`), effect slots
(`0x0b`) and a refresh. `docs/origins.md` records how each field was measured on the GS76,
send by send. This work builds on
[msi-perkeyrgb](https://github.com/Askannz/msi-perkeyrgb) (the steady-color protocol and
the captured "aqua" preset used as effect templates),
[Keyglow](https://github.com/beylerburak/keyglow) and
[msi-laptop-ec-protocols](https://github.com/StevenHrovat/msi-laptop-ec-protocols)
(effect fields, measured on a GE76). Key positions and some field meanings were read from
SteelSeries GG's device descriptions; none of its files are part of this project.

## Development

`bin/test` builds and runs the tests offscreen, `bin/build` builds, `bin/install` builds
and installs the package from this tree (`pkgbuild/`). The packet encoder is checked byte
for byte against packets recorded from msi-perkeyrgb. Optional tools: `tools/gen_layout.py`
regenerates the key table (needs msi-perkeyrgb; without GG's GE76 file it keeps the
positions already in `src/layoutdata.cpp`), `tools/gen_fixtures.py` records reference
packets (needs msi-perkeyrgb).

## License

MIT, see `LICENSE`.
