---
name: debug-overlay
description: Toggle the in-panel visualisation of collision obstacles. Use when bristles aren't reacting where expected, when adjusting the cable formula, or when verifying coordinate-space assumptions for a new obstacle type.
---

The debug overlay is gated by a `#define` at the top of `src/Duster.cpp`:

```cpp
#define DUSTER_DEBUG_OBSTACLES 0   // change to 1 to enable
```

When enabled, `BristleWidget::draw` renders three things on top of the
panel:

| Visual | Meaning |
|---|---|
| **Cyan rectangles** with thin outlines | Every `ParamWidget` and `PortWidget` from neighboring modules. Their position is the rect's screen rect — if they don't line up with the visible knob, the coordinate space is wrong. |
| **Green tubes** (thick lines) | Cable segments. There are `kCableSamples = 24` per cable, traced along the quadratic-bezier approximation. Should overlap the actual rendered cable across the full Cable Tension range. |
| **Red bar in the top-left corner** | Width grows with `segObstacles.size()`. If this bar stays empty in a patch with cables, `rack->getCables()` is returning nothing — usually a sign that `inputPort`/`outputPort` was null. |

To toggle: edit `src/Duster.cpp` line ~9, flip `0` ↔ `1`, then run the
`build` + `install` skills.

Don't ship with the overlay on — it interferes with the visual. Flip
back to `0` before any commit that's meant for release.
