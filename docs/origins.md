# Wave origins in the KLC effect packets — UNVERIFIED

Offline analysis for Task 13. Nothing here has been sent to the keyboard.

`msi-kbd-effect` rewrites only the transition table (0x02–0x81), the start color
(0x84–0x89), the transition count (0x96) and the total duration (0x98–0x99); it keeps
bytes 0x8A–0x95 and 0x9A from the captured `aqua` preset. `tools/decode_origins.py`
prints them:

| id | 0x8A | 0x8C (i16) | 0x8E (i16) | 0x90 | 0x92 | 0x94 | 0x9A |
|---|---|---|---|---|---|---|---|
| 0 (pulse) | 255 | 1500 | 0 | 0 | 1 | 192 | 0 |
| 1 | 255 | −3 | 614 | 1 | 0 | 327 | 1 |
| 2 | 255 | 2344 | 486 | 1 | 0 | 306 | 0 |
| 3 | 255 | 0 | 550 | 1 | 0 | 327 | 0 |
| 4 | 255 | 4294 | 550 | 1 | 0 | 327 | 0 |
| 5 | 255 | 1602 | 546 | 1 | 0 | 327 | 0 |

Proposed mapping against `lighting_effect` in SteelSeries GG's `common_lighting.lisp`
(`type`, `has_direction`, `direction_type`, `direction_inverted`, `focal_x`, `focal_y`,
`speed`, `scale`):

- 0x8C / 0x8E: `focal_x` / `focal_y`, the wave origin. The x values span 0–4294 and
  the y values 486–614, so the unit is not GG's layout unit (x 140–807); the scale
  factor is unknown.
- 0x90: wave on (`has_direction`), 0x92: breathing (`type` breathe) — the pulse is the
  only packet with 0x90 = 0 and 0x92 = 1.
- 0x94: propagation `scale`.
- 0x9A: `direction_inverted` (only effect 1).
- 0x8A: likely a brightness or level, 255 everywhere.

To verify (with the user, one send): copy effect 3, set 0x8C to the value of effect 4
(4294) and check the wave now starts from the right side. Until then, origins stay
limited to `aqua-0` … `aqua-5`.

## Cross-check with Keyglow (2026-11-04)

Keyglow 0.1.0 (github.com/beylerburak/keyglow, GPL-3.0, tested on a Monster Semruk
`1038:113a`) builds effect packets from scratch instead of patching `aqua`. r-rgb
(github.com/MikhaelCat/r-rgb) carries a line-for-line Rust port of the same
`protocol.py`, so the two count as one source. Read for protocol facts only; no code is
copied (GPL).

Its field map for 0x8A–0x9A, against the table above:

| bytes | Keyglow | our guess | agrees |
|---|---|---|---|
| 0x8A | always 0xFF | brightness or level | — |
| 0x8C (u16) | wave origin x, 0..0x105C | `focal_x` | yes |
| 0x8E (u16) | wave origin y, 0..0x040D | `focal_y` | yes |
| 0x90 (u16) | propagate along x | wave on | partly |
| 0x92 (u16) | propagate along y | breathing | **no** |
| 0x94 (u16) | wavelength, 0x1F..0x3E9 | `scale` | yes |
| 0x9A | inward (wave runs toward the origin) | `direction_inverted` | yes |

Read this way, `aqua-0` is not a breathing effect but a wave running down from the top
edge (x ignored, along y only), and its directions map as Keyglow's: right = x 0 along
x, left = x max along x, down = y 0 along y, up = y max along y, out = centre along both,
in = centre along both + inward. Keyglow also uses up to 9 effect slots (8 for its
Starlight effect); we stop at the 6 seen in `aqua`.

### Transition slopes: our model did not fit the capture — CONFIRMED, fixed

Keyglow reads each transition's signed bytes as a per-time-unit slope in 1/16 color
level: a transition changes a channel by `slope * duration / 16`. We (after
`msi-kbd-effect`) read them as a total change of `2 * slope` levels, whatever the
duration. `tools/decode_slopes.py` replays the captured cycles under both models:

- `aqua-0`: slopes ±80 over 51 units. Keyglow's model walks the hue wheel exactly
  (red, yellow, green, cyan, blue, magenta, red: each step 80·51/16 = 255 levels). Ours
  gives (255,160,0), (95,160,0)… and ends at blue −20, outside 0..255.
- `aqua-1` and `aqua-2` leave a cycle error of 20–42 levels under ours, under 4 under
  Keyglow's.
- The two models agree only when a transition lasts 32 units. Most `aqua` steps last
  33, which is why patching `aqua`'s colors looked right.

Consequence if Keyglow is right: any of our steps far from 32 units (Tron's 55 and 40,
any speed other than 100 %) overshoots or falls short by `duration / 32`, and the
controller wraps the channel instead of clamping it. The preview interpolates the
intended colors, so it would not show the difference. Keyglow's time unit (10 ms,
"verified on hardware") matches our `Preview::UnitMs`. It counts the reactive duration
in milliseconds, which would make our default of 30 a 30 ms flash, too short to see.

Confirmed on the GS76 on 2026-11-04: with Tron applied, the keys stayed orange where the
preview turned red, as Keyglow's model predicts (#F04C06 instead of #FF000D). `transitions()`
now encodes per-unit slopes: 8.4 fixed point, rounded toward zero so no step overshoots,
and a step too short for its change is lengthened (a full swing needs 33 units). The
`aqua-0` table is reproduced byte for byte (`effectsMatchesCapturedPulse`). The old encoding
also wrapped the rainbow and breathing base effects (40 and 80-unit steps).

### One send to settle the wave fields (slopes already settled)

Build a single test profile: every key on one effect slot, `aqua-3`'s packet with
0x8C set to the right edge, and one cycle black → white over 100 units (1 s), hold 50,
white → black over 100, slopes encoded Keyglow's way (±40 = 255·16/100, rounded toward
zero). Expected if Keyglow is right: a smooth 1 s fade to white, then back, sweeping in
from the right. Under our encoding the same cycle would be slopes ±127 and flicker
through several wraps, so the outcome tells the two models apart without ambiguity.

## Wave fields measured on the GS76 (2026-11-04)

Eleven single sends from a throwaway build (base wave white → blue, 60 + 60 + 60 units,
`aqua-3`'s packet with only the fields below changed). What the user saw:

| x (0x8C) | y (0x8E) | 0x90 | 0x92 | 0x94 | 0x9A | seen |
|---|---|---|---|---|---|---|
| 0 | 518 | 1 | 0 | 327 | 0 | wave runs right → left |
| 4188 | 518 | 1 | 0 | 327 | 0 | wave runs left → right |
| 2094 | 0 | 0 | 1 | 327 | 0 | all keys in step, no wave |
| 2094 | 1037 | 0 | 1 | 327 | 0 | all keys in step, no wave |
| 2094 | 518 | 1 | 1 | 327 | 0 | edges → centre |
| 2094 | 518 | 1 | 1 | 327 | 1 | centre → edges |
| 2094 | 518 | 1 | 0 | 327 | 0 | edges → centre, same as with 0x92 = 1 |
| 2094 | 0 | 1 | 1 | 327 | 0 | from the edges, rounded fronts |
| 2094 | 32000 | 1 | 0 | 327 | 0 | converges right of centre, not downward |
| 2094 | −32000 | 1 | 0 | 327 | 0 | same as 32000 |
| 0 | 518 | 1 | 0 | 100 | 0 | right → left, narrower bands |

So, on this controller:

- 0x90 and 0x92 are the x and y scaling of the distance (see below): `aqua-0` (both 0)
  is a plain pulse. The "no wave" seen with 0x90 = 0, 0x92 = 1 was a vertical wave too
  fast to see at 0x94 = 327.
- The phase follows the distance to the point (0x8C, 0x8E), in both x and y: fronts are
  circles around it, not lines.
- 0x9A = 0 makes the wave run toward the point, 1 away from it — the reverse of
  Keyglow's naming.
- 0x94 is GG's `scale`, the propagation speed (pixels per tick): a key lags by its
  distance divided by it, so smaller values give narrower bands.
- The point must stay on or near the keyboard: at |y| = 32000 the result is wrong (likely
  an overflow in the controller), so a straight vertical wave is out of reach. Distances
  across the board stay within x 0..4294, y 0..1037.

## SteelSeries GG's own encoding (decrypted `fancy_lighting_engine.lisp`)

GG builds the effect payload from its `lighting_effect` struct (`libsteel_lighting_graphics`):
transition sections, start color × 16 (8.4 fixed point), `255 0` (0x8A), `focal_x`
`focal_y` (0x8C, 0x8E), x scaling (0x90), y scaling (0x92), `scale` (0x94), section
count (0x96), total ticks (0x98), `direction_inverted` (0x9A). The scalings follow
`direction_type` (`common_lighting.lisp`):

| direction_type | 0x90 | 0x92 |
|---|---|---|
| horizontal (0) | 1 | 0 |
| vertical (1) | 0 | 1 |
| radial (2) | 1 | 1 |

`scale` is "propagation speed (pixels per tick)". GG's `ss-klc-base` also declares effect
slots `effect_0` … `effect_18`, beyond the 6 we use.

Two more sends (2026-11-04), vertical (0x90 = 0, 0x92 = 1) at the slowest speed, 31,
origin at the top then the bottom: rows alternate light and dark blue in both, so the
controller does know each key's height; at 327 that lag was too small to see.

## Key packets and the power key (2026-11-04)

Byte 2 of a `0x0e` key packet is not a region id but the number of key elements in the
packet, as GG's `key_illumination_block_*` structs name it (`length`): 0x2A = 42, 0x24 =
36, 0x18 = 24, 0x0B = 11, which is exactly how many keys each captured packet holds.

The GS76 inherits GG's `msi-ge76-klc`, whose notes list "a 'power' key on the top-right
corner", HID 0x66, absent from msi-perkeyrgb's lists and from the GS75 `aqua` capture.
Sent as a 12th element of the 11-key packet (byte 2 = 0x0C), with the rest steady cyan:
red showed orange, blue showed blue — the key has a full RGB LED. A small orange light
stays beside it in both cases: a separate indicator, not on the KLC (neither msi-ec nor
the kernel LED class exposes it).

GG also gives each GE76 key's position (`get-prism-sync-nonvariant-hid-info`, x 140..807,
y 89..256, y growing upward: the space bar is at 89, the F row at 256; power at 807, 256).
Scaled by 4294 / 667 ≈ 6.44 in both axes, that span is 4294 × 1075, the controller's own
range: these are very likely its units, up to an offset.

## Directions checked on the GS76 with klc-light 0.3.0 (2026-11-04)

One send each, base wave white → blue, size 68, plus the power key:

| profile | seen |
|---|---|
| static cyan, power key green | power key green (packet of 12 elements, no code-0 filler) |
| toward the left / right | left / right, as expected |
| toward the top (y 1075, GG's "up") | fronts running **down** |
| toward the bottom (y 0) | fronts running **up** |
| toward / out from the centre | as expected |
| out from Esc, toward keypad Enter | rings from Esc, closing on keypad Enter |

So the controller's y grows downward, unlike GG's table: positions are now
y' = (256 − y_GG) · 6.44, the F row at 0 and the space bar at 1075, and `top` is y 0.
The radial checks could not tell, since at size 68 the height adds little lag.

Re-checked after the flip: toward the top runs up to the F row, toward the bottom runs
down to the space bar, and a small radial wave out from the space bar (size 15) rings
upward and outward from it. Every direction now matches the editor.

## The power key is F1's twin (2026-11-04)

The first power-key checks were misleading: with the rest of the board lit, the power
key looked "red but orange" or turned yellow with the wave. Bisecting with everything
else black (power red as element 0x66, groups of keys set to green) found the cause:

| sent (everything else black) | seen |
|---|---|
| power red | power red |
| power red, the 12 hidden codes green | power red |
| power red, F1 green (bisection down to one key) | power **yellow** |
| power green, F1 black | power green **and keypad 3 green** |
| F1 magenta, no power element | F1 and power **magenta** |
| F1 red, no power element | F1 red, power red (duller, tinted by the orange indicator beside it) |

The power key's LED is driven by F1's output; code 0x66 adds to it and also lights
keypad 3. klc-light therefore sends no power element (the 11-key packet is back to
11) and treats the power key as F1's twin: drawn in F1's color, selecting F1.

## Reactive keys (2026-11-04)

Measured with a different reactive duration per row (50, 150, 400, 1000, 2000): the
pressed color lasts about that many **milliseconds**, as msi-perkeyrgb's effectdoc says
(the vendor uses 100–1000) and GG's `reactive_lighting.time` (0..2000) implies.

Pressing some keys also lights a neighbour briefly: AZERTY Q, S, D light A, Z, E; R, T, Y
light 9, 0, ). With a pressed color per row, the neighbour shows only the **green** part:
a blue pressed color shows no neighbour at all, yellow or white show green. Emptying the
nine effect slots first, or linking reactive keys to an unused effect id (17), changes
nothing. A static send with only AZERTY Q and R green also glows A and 9 faintly: it is
ghosting in the keyboard's LED matrix, strongest on green (the lowest forward voltage),
not something the packets cause. GG sends the same reactive elements (mask 8).

Other projects' notes used here: msi-perkeyrgb's effectdoc (reactive duration in ms,
"effect id appears random in reactive"), StevenHrovat/msi-laptop-ec-protocols (GE76
measurements: slopes per 10 ms tick, 0x9A = 0 toward the origin, a `0x0C` per-key
overlay that lights the power key by its own id 0x66, the latch law for running slots),
OpenRGB's SteelSeries ApS branch (layout query `0x10`, free-order `0x0E` packets).

## The power key and the 0x0C overlay (2026-11-04)

The GE76 notes' per-key overlay, `0C 00 66 00` + `[66 R G B]` (unused bytes `FF`), then
`0D 00 02`, sent after the usual burst:

| burst | F1 | overlay asks power | power shows |
|---|---|---|---|
| static, all black | off | red | red, nothing else lit |
| Tron (normal send, no overlay) | cyan | — | cyan: the next burst clears the overlay |
| Tron (wave base) + overlay, also with a 500 ms pause first, sent before the closing `09`s, or ending with `09` | cyan | red | cyan |
| static, F1 cyan | cyan | red | cyan |

So the power key's LED works, but on the GS76 it shows F1's color whatever is sent; its
own setting (overlay, or code 0x66 in a table, which also lights keypad 3) only shows
while F1 is off, or mixed in. klc-light keeps the power key as F1's twin and sends no
overlay. The GE76 lights it on its own: the link is particular to the GS76.

## Eighteen effect slots (2026-11-04)

GG's `lighting_element_info.effect_index` ranges 0x00..0x11 and `ss-klc-base` declares
effect reports up to `effect_18`. One send with 18 one-key pulse groups (F1..F12, 1..6,
slots 0..17, each its own color; the six captured slots first in their order, then
6..17): all 18 keys pulsed. Caps Lock, S and T glowed faintly green in step: LED-matrix
ghosting from their lit neighbours, as with reactive keys. klc-light allows 18 animated
groups.

## Attaching the controller without root: sysfs `authorized` (2026-11-04)

Until 0.4.0 KLC Light attached the controller to the kernel's `usb` driver through a
root helper (`echo <port> > /sys/bus/usb/drivers/usb/bind`, then `unbind` after the send,
on the belief that the controller only shows a new burst once detached; with `authorized`
it shows it while attached, see 2026-11-11 below). Measured on the GS76 instead:

- With the controller left without its driver by that `unbind`, writing `1` to its
  `authorized` file authorises it ("authorized to connect") but nothing configures it: no
  driver, `bConfigurationValue` empty, no hidraw. The driver has to be bound once (a full
  power-off does it at boot).
- With the driver bound, `authorized` 0 → 1 reconfigures the controller (two interfaces,
  hidraw back), the burst is sent, and `authorized` 0 detaches it; the driver stays
  bound. Two cycles in a row (Jarvis, then Tron) showed the right colors with no error.
- After a full power-off the controller comes back authorised with its driver; the
  package's udev rule gives `authorized` to the `klc-light` group (mode 664) at each
  `add`, and the boot hook's `klc-light apply` showed the profile without sudo.

So 0.5.0 drops the helper and its sudoers rule: `SysfsBinder` only writes that one file.

## What shows while attached, and GG's host-stream mode (2026-11-11)

Measured on the GS76 (klc-light 0.5.1 installed) with a throwaway
program that attaches through `authorized`, sends, holds the controller attached for 8 s
and only then detaches. No send failed and the kernel logged nothing in any run.

### A burst shows at once, attached

| burst | ends with | while attached | at detach |
|---|---|---|---|
| static, all red | `0D 00 02`, no `09` | red at once | stays red |
| Rainbow (animated base) | `0D 00 02`, no `09` | shown and moving at once | — |
| Ember (wave and pulse), as klc-light sends it | feature `09`, output `09` | shown and moving at once | no change |

So the controller does not need to be detached to show a burst, and either ending shows
it, effects included. GG names the two: output `09` is `save_to_flash`, output `0D 00 02`
is `reset_current_lighting_mode`, which it sends after a live deploy (`post-live-deploy`
in `ss-klc-base.lisp`). Not measured: whether `09` really writes the profile to flash on
this firmware (what the keyboard shows after a full power-off, before the boot hook), and
whether the controller can stay attached across suspend and restarts. klc-light keeps
ending with `09` and detaching until those are.

### Host-stream frames (`0C`)

`common_lighting.lisp` names the per-key `settings_mask` values: 0 effect, 1 steady,
2 host-stream, 4 override, 8 reactive. GG's game integration and PrismSync (one frame every
33 ms, `prism-sync-boilerplate.lisp`) put keys in mask 2 with the usual `0E` region
packets, send `0D 00 02` once, then stream feature reports `0C 00 <n> 00` followed by
n × `[hid R G B]` (`direct_write_raw`). Its audio visualizer draws on a 22 × 6 bitmap
mapped to keys by `hidcode-matrix-layout-nonvariant` in `msi-ge76-klc.lisp`.

Sent here to all 113 keys of the four region packets (mask 2, black, 30 ms apart, then
`0D 00 02`), unused bytes of each frame `FF`:

| frames | result |
|---|---|
| 5 at 1 per second, 113 keys in one packet | keyboard black: the mask took, no frame shown |
| 3 at 1 per second, split in packets of 57 and 56 keys, 10 ms apart | each color shown |
| 3 more, each followed by `0D 00 02` | brief flashes: `0D 00 02` clears the frame |
| 300 at 1 per 33 ms, split the same way, twice | smooth; 1.3 ms per packet, 1.6 ms at most |

A normal `klc-light restore` after each stream showed the profile again. GG caps a frame
at `number-of-elements` keys, 110 for the GE63 family the GS76 derives from, which fits
113 failing; the exact limit, and streams longer than 10 s, are not measured.

## The music visualizer on the GS76 (2026-11-11)

klc-light 0.6.0, installed as a package, the visualizer started and stopped through its
user unit unless said otherwise. The kernel logged nothing about the controller in any of
these runs, and it was left detached (`authorized` 0) after each.

| run | seen on the keyboard | on the system |
|---|---|---|
| one minute of music, then `visualizer off` | the spectrum; the applied profile back at once | `on` returns in 0.35 s, `off` in 0.28 s |
| `klc-light apply Ocean` 15 s into the stream (Jarvis applied) | the spectrum takes Ocean's colors with no break; Ocean shown after `off` | `apply` returns in 0.07 s, nothing sent by it |
| `kill -9` of the service 10 s into the stream | the profile back by itself | unit `failed` (signal), `ExecStopPost` ran `klc-light restore`: detached within a second |
| suspend (s2idle) for 21 s with the stream running, then wake | the profile shown before sleeping, the spectrum back a few seconds after waking | still detached 2 s after waking, attached again by 6 s, unit still active |
| one stream of 12 min 16 s, across that suspend and another of 28 s | — | 5.9 s of CPU time, 7.4 MiB at most; stopped cleanly |
| stopped and started three times within 45 s (09:36:49 to 09:37:34) | — | each start waited out the interval (3 s from "Starting" to "Started"); all clean |

So a `restore` after the service is killed mid-stream does take the controller out of
host-stream mode, and the controller comes back after a suspend when it was detached
first and attached again five seconds after waking. Holding it attached across a suspend
is still not measured, and neither is a stream longer than those twelve minutes.

### Tuning the spectrum

With one reference for all bands over 30 dB (the first version), only the five leftmost
columns ever lit on real music: its energy is in the bass. Replayed on a 20 s recording of
that music, the mean bar height per column, out of 6:

| version | columns, left to right |
|---|---|
| first | 3.5 3.6 3.7 4.2 2.8 0.7 0.3 0.4 0.2 0.5 0.4 0.3 0.1 0.1 0.0 0.0 0.0 0.0 |
| + 4.5 dB per octave, 4096-sample window | 2.3 3.2 3.7 4.4 3.5 1.7 1.6 2.0 2.0 3.0 3.2 3.3 3.3 3.6 3.7 3.7 3.6 2.9 |
| + each band against its own peak (forgotten at 6 dB/s), 15 dB range | 3.5 3.5 2.8 3.4 4.4 2.6 2.2 3.1 2.9 4.3 3.7 3.8 4.2 4.7 4.6 3.4 4.3 4.1 |
| + peaks forgotten at 0.6 dB/s, 12 dB range (kept) | 3.2 2.7 2.4 2.6 3.0 2.1 1.6 2.6 1.5 3.5 2.7 2.4 2.4 3.7 3.6 2.5 3.4 2.2 |

On the keyboard, in the same order: "moves, badly spread", "better, still hollow", "too
full", "good". One piece of music only; the constants are in `src/spectrum.h`.
