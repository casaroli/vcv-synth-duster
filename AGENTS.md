# AGENTS.md — guidance for AI agents working on this repo

This file is meta. It tells future AI coding agents (Claude Code, Cursor,
Codex, whoever) what this project is, what its quirks are, and what *not*
to redo because we already tried it.

## What this is

**Synth Duster** is a VCV Rack 2 plugin that adds a single decorative 8 HP
module shaped like a Befaco Synth Duster brush. It has no audio, no CV,
no params except a single view-mode switch. Its entire reason for
existing is the **per-bristle physics simulation** — 101 bristles that
react to neighboring modules' knobs, jacks, and cables in real time.

It is a joke. It is also a real plugin that builds, installs, and runs.
Both things are true.

The user is the project owner and primary tester. They run Rack locally
on macOS arm64.

## Build / install

The Rack 2 SDK lives at `~/Rack-SDK`. Build with:

```bash
make RACK_DIR=$HOME/Rack-SDK         # builds plugin.dylib
make install RACK_DIR=$HOME/Rack-SDK # builds + packages + drops into Rack's user dir
make clean                           # blow away build/
```

Project-local Claude Code skills shortcut these — see `.claude/skills/`.

### Expected non-issue: clang IDE diagnostics

The IDE will show a wall of clang errors like:

```
'rack.hpp' file not found
Use of undeclared identifier 'config'
Use of undeclared identifier 'widget'
...
```

**Ignore them.** They're real to clang because clang doesn't know about
the SDK include path. The actual `c++` invocation in the Makefile passes
`-I$(RACK_DIR)/include`, which resolves all of them. If `make` succeeds,
the build succeeded. Don't waste cycles "fixing" these.

## File layout

| Path | Role |
|---|---|
| `src/plugin.{cpp,hpp}` | Standard VCV plugin entry + model registration. Unlikely to change. |
| `src/Duster.cpp` | **Everything interesting.** Module, BristleWidget (physics + render), DusterWidget (panel, drag, snap, mode switch). |
| `src/Bristles.inl` | **Generated.** 101 `BristleDef` records produced by `tools/gen_bristle_data.py`. Don't hand-edit. |
| `res/Duster_handle.svg` | Side-view panel (handle, wood grain, label). |
| `res/Duster_handle_top.svg` | Top-view panel (wood from above, narrower so bristles can fan out the sides). |
| `res/Duster_bristles.svg` | Bristle source data — never rendered at runtime, parsed by the generator. |
| `tools/gen_bristle_data.py` | Reads `Duster_bristles.svg`, emits `src/Bristles.inl`. |
| `images/duster_demo.gif` | The animated README demo. The `.mov` source is gitignored. |

## Key concepts

### Mode switch (side vs top)

A single `Duster::MODE_PARAM` toggles between two views:

- **Mode 0 — side:** bristles rendered as red lines from `(d.anchorX, d.anchorY=69)` extending up to `(d.restX + dispX, d.restY + dispY)`. Drag-lag enabled — bristles trail brush motion.
- **Mode 1 — top:** bristles distributed around the **perimeter** of the wood block (defined by `kTopHandleX0..Y1`). At rest each bristle's projection collapses to a point inset 1.5 mm inside the wood — hidden. Any displacement stretches it outward along its edge's normal, with a tangential sway. Drag-lag *disabled* (no surface friction in 2-D).

The `BristleWidget` reads the param via `dusterModule->params[Duster::MODE_PARAM]` each step and branches the physics/rendering. `DusterWidget::step()` watches the same param to swap the SvgPanel background.

### Z-order trickery (important)

`setPanel(panel)` inserts the panel near the *front* of children, which by default puts the `BristleWidget` *after* (in front of) it. The wood would not cover the bristle bases. To fix it, the constructor does:

```cpp
addChild(bristleLayer);
children.remove(bristleLayer);
children.push_front(bristleLayer);
```

after `setPanel`. **Don't undo this** — without it, top-mode bristles render on top of the wood instead of behind it.

### Cable physics (matches Rack exactly)

The cable collision approximation **is not** a generic bezier. It's the
exact slump formula from VCV Rack's own `app/CableWidget.cpp`:

```cpp
control.y = midpoint.y + (1 - settings::cableTension) * (kCableSagBaseline + kCableSagPerPx * dist);
```

with `kCableSagBaseline = 150` and `kCableSagPerPx = 1.0` — those are the
exact constants from Rack source (`150 + dist`). Each cable is sampled
into 24 segments. **If a future agent thinks the cable physics looks
"off", check `settings::cableTension` first** — the formula is right, the
sag scales with the user's Cable Tension setting.

### Body-sampled collisions

Each bristle is tested against obstacles at **four points along its
length** (t = 0.25, 0.5, 0.75, 1.0), not just the tip. So small jacks
near the brush base actually push the bristle aside. Critical for the
top mode where the bristle "tip" is barely offset from the anchor.

### Coordinate spaces

The trap here: `getInputPos()`/`getOutputPos()` on `CableWidget` are
ambiguous. The code computes port centers explicitly via:

```cpp
port->getRelativeOffset(port->box.size.div(2.f), rack->getModuleContainer())
```

with `moduleContainer` as the ancestor (not `rack`!). This puts cable
endpoints in the same coordinate space as `mw->box.pos.plus(pw->box.pos)`
that the rectangular obstacles already use. Walking up to the rack
instead would add an unwanted `moduleContainer.box.pos` offset.

## Things already tried that didn't work

Don't redo these unless you have a new approach:

1. **`drawLayer(args, 1)` to render above cables.** Cables themselves draw in layer 1 too (`CableWidget::drawLayer` is overridden), and the cable container is iterated after the module container. We tried `layer == 2`; Rack doesn't iterate that deep — the module disappeared entirely. Verdict: the "render above cables" feature is architecturally infeasible without a sibling overlay widget hack. The README never claims this works.
2. **Extending top-mode side peeks off-panel.** Z-order with neighbor modules is inconsistent, so off-panel pixels may or may not be visible. We solved it by narrowing the top-view wood block in the SVG to give side peeks ~5 mm of in-panel margin.

## Tuning constants (top of `BristleWidget`)

Side mode physics: `kStiffness`, `kDamping`, `kDragGainX/Y`, `kContactDamping`, `kMaxDispMm`.

Top mode physics: `kTopStiffness` (softer — weak contacts still register), drag-lag is disabled.

Top mode rendering: `kTopHandleX0..Y1` (perimeter rect, **keep aligned with the wood-block rect in `Duster_handle_top.svg`**), `kTopAnchorInset`, `kTopPeekScale`, `kTopPeekLatScale`.

Cable: `kCableRadiusPx`, `kCableSamples`, `kCableSagBaseline`, `kCableSagPerPx` (last two: don't change without re-verifying against Rack source).

Snap-on-release: thresholds inside `DusterWidget::onDragEnd` (1 HP horizontal, 1/4 row vertical).

## Workflow conventions

- The user commits when they say "commit", not before.
- The user reviews visually in Rack between iterations. Don't bundle five UI changes into one rebuild — they want to see each one.
- Generated artifacts (`src/Bristles.inl`) are committed. Re-run the Python tool whenever `res/Duster_bristles.svg` changes.
- The `DUSTER_DEBUG_OBSTACLES` define at the top of `Duster.cpp` toggles obstacle visualisation (cyan rects, green cable tubes, red segment-count bar). Useful when collision feels wrong.

## TODOs the README jokes about

The README's TODO section is mostly a bit, but two items are real
concerns worth noting:

- **Off-panel side peeks** would let top-mode bristles fan dramatically beyond the 5 mm margins, but require a render layer outside the ModuleWidget (sibling under RackWidget, drawn after `cableContainer`).
- **"Convert to MetaModule"** is funny precisely because the 4ms MetaModule runs VCV plugins on real hardware. If somebody actually wants this, it's a real port.

Everything else in the TODO list is a gag.
