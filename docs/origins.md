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
