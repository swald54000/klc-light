#!/bin/sh
# Copies the bar widget into a checkout of the plugin repository (swald54000/omarchy-klc-light),
# whose root must be the plugin itself for `omarchy plugin add`. Usage: publish_plugin.sh <checkout>
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DEST="${1:?usage: publish_plugin.sh <plugin repository checkout>}"
SRC="$ROOT/omarchy-plugin/swald.klc-light"
find "$DEST" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -R "$SRC"/. "$DEST"/
cp "$ROOT/LICENSE" "$DEST/LICENSE"
cat > "$DEST/README.md" <<'README'
# KLC Light bar widget for Omarchy

A bar widget for [KLC Light](https://github.com/swald54000/klc-light), the per-key keyboard
lighting editor for MSI laptops (SteelSeries KLC controller). A keyboard icon, dimmed while
the lighting is off, whose tooltip names the applied profile. Click it for the profile list
(a click applies one), right-click to turn the keyboard off, middle-click to turn it back on;
the panel also opens the editor.

Needs [KLC Light](https://github.com/swald54000/klc-light) installed (see its README:
build it with the PKGBUILD in `aur/`, or install the package attached to its latest
release), and your user in the `klc-light` group.

```sh
omarchy plugin add https://github.com/swald54000/omarchy-klc-light.git --enable
```

To remove it:

```sh
omarchy plugin remove swald.klc-light
```

It only runs the `klc-light` command line (`list`, `apply`, `off`) and reads
`~/.config/klc-light/state.json`; it changes no configuration. MIT license.
README
omarchy plugin validate "$DEST"
echo "plugin copied to $DEST"
