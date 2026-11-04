# Omarchy plugin marketplace submission

Form: https://github.com/omacom/omarchy-plugin-marketplace/issues/new?template=submit-plugin.yml

- **Title:** `[Plugin]: KLC Light`
- **Repository URL:** https://github.com/swald54000/omarchy-klc-light
- **Category:** Hardware
- **Tags:** Bar, Quickshell
- **Suggest a missing tag:** Keyboard
- **Maintainer notes:**

  > Bar widget (id `swald.klc-light`) for KLC Light, a per-key RGB lighting editor for MSI
  > laptops with a SteelSeries KLC keyboard controller (USB 1038:113a), tested on an MSI
  > GS76. It shows the applied profile, applies another from its panel, turns the keyboard
  > off and on, and opens the editor. It only runs the `klc-light` command line (`list`,
  > `apply`, `off`) and reads `~/.config/klc-light/state.json`; it writes no configuration
  > and asks for no privilege. Dependency: KLC Light (https://github.com/swald54000/klc-light: build it
  > with the PKGBUILD in `aur/`, or install the package attached to its releases; the AUR
  > package follows once AUR registrations reopen); its package adds a `klc-light` group and a
  > udev rule so the controller is attached without root. Without KLC Light the widget
  > says so and offers nothing else. MIT license.

- **Checklist:** all five boxes (public repo with install and removal instructions;
  license and dependency documented; own work; no configuration overwritten; listing is
  not a security review).
- **Preview:** a screenshot of the open panel, taken by the maintainer.
