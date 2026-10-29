# KLC Light

Per-key lighting editor for the SteelSeries KLC keyboard of the MSI GS76
(`1038:113a`), built for Omarchy: one lighting profile per theme, an on-screen
preview, and `klc-light apply` for the theme hooks. Nothing reaches the
keyboard until you press Apply, and each Apply sends once: this controller
hangs on repeated sends.

## Use

- `klc-light` opens the editor on the active theme's profile.
- `klc-light apply` sends the active theme's lighting (its `keyboard.klc.json`,
  else `keyboard.effect`, else `keyboard.rgb`).
- `klc-light apply --check` builds and validates without sending; add `--json`
  for machine-readable output.
- `KLC_LIGHT_FAKE_DEVICE=1 klc-light` prints the packets instead of sending.

Profiles are saved to `~/.config/omarchy/themes/<theme>/keyboard.klc.json`.

| Shortcut | Action |
|---|---|
| Space | Apply to the keyboard |
| Ctrl+Z / Ctrl+Shift+Z | Undo / redo |
| Esc | Clear the selection |
| ? | Show the shortcuts |
| Q | Quit (asks if changes are unapplied) |

## Build

`bin/test` runs the tests offscreen, `bin/build` builds, `bin/install`
installs the package. The packet encoder is checked byte for byte against
packets recorded from msi-perkeyrgb (`tools/gen_fixtures.py`).
