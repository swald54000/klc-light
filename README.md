# KLC Light

Per-key lighting editor for the SteelSeries KLC keyboard of the MSI GS76
(`1038:113a`), built for Omarchy: a library of named lighting profiles that
themes can link to, key groups with their own effects, an on-screen preview,
and `klc-light apply` for the theme hooks. Nothing reaches the
keyboard until you press Apply, and each Apply sends once: this controller
hangs on repeated sends.

## Use

- `klc-light` opens the editor on the profile last applied. The bar at the top
  creates, duplicates, renames and deletes profiles, and links the open profile
  to a theme.
- `klc-light apply` sends the last applied profile.
- `klc-light apply --theme` sends the profile linked to the active theme (the
  one named in `~/.local/state/omarchy/current/theme.name`). A theme without a link
  has nothing to send: it exits 0 and touches nothing.
- `klc-light apply <name>` sends the library profile of that name;
  `klc-light apply <file.json>` sends a profile file (a name containing `/` or
  ending in `.json` is read as a file).
- `klc-light apply ... --check` builds and validates without sending and prints
  `N packets OK`; add `--json` for machine-readable output
  (`{"message":"7 packets OK","ok":true,"packets":7,"source":"Tron"}`). `--json` works with sending too.
- Exit status: 0 on success or when there is nothing to apply, 1 on any error
  (message on stderr), 2 when the keyboard is absent from the USB bus.
- `KLC_LIGHT_FAKE_DEVICE=1 klc-light` prints the packets instead of sending.

## Profile library

Profiles are plain files in `~/.config/klc-light/profiles/<name>.klc.json`.
`~/.config/klc-light/state.json` records which profile was applied last and
which profile each theme is linked to. A corrupt `state.json` is moved aside as
`state.json.bad` rather than overwritten.

On first run (when `profiles/` does not exist yet) the library is migrated from the old per-theme files
(`~/.config/omarchy/themes/<theme>/keyboard.klc.json`, else `keyboard.effect`,
else `keyboard.rgb`, the user's copy before the stock one): each theme with lighting becomes a profile named after the
theme and linked to it. The theme files are left untouched. With nothing to
migrate, a `Default` profile is created.

Hooks: the Omarchy `theme-set` hook should call `klc-light apply --theme` so a
theme switch loads its linked profile, and the boot hook should call
`klc-light apply` to restore the last applied one.

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
