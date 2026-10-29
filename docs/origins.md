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
