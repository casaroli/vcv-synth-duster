# Synth Duster VCV Rack Plugin

> Did you ever want to try the full potential of the Befaco Synth Duster but never had the resources to acquire one, or the time to wait for Behringer to make a cheap clone? **Now you have the unique opportunity to experience the full potential of the Befaco Synth Duster for FREE in VCV Rack 2.**

![hero shot of the Synth Duster sitting smugly between two oscillators](docs/hero.png)
<!-- TODO: replace with a real screenshot. 8HP, transparent panel,
     bristles vertical, casually parked in a busy patch. -->

A physically-simulated 8HP brush for VCV Rack 2. Zero CV. Zero audio. Zero practical purpose. **Maximum vibes.**

## Live demo

https://github.com/your-username/duster/assets/000000/PLACEHOLDER
<!-- TODO: 10-15s screen capture: drag the Duster across a busy rack,
     watch individual bristles catch on knobs, jacks, and cables. -->

## Features

- **8 HP.** The exact same width as the original. We measured.
- **Transparent panel.** You can see the rack rails *right through it*. It's not a module. It's a tool.
- **Per-bristle 2-D physics.** 101 independent bristles with their own displacement, velocity, spring stiffness, and damping. Each one is on its own journey.
- **Real-time collision with knobs and jacks.** Drag the brush across a Frequency knob and the bristles bend around it like they're meant to.
- **Real-time collision with patch cables.** Each cable is sampled as a sagging bezier into line segments. Your wires push the bristles.<sup>†</sup>
- **Free-floating placement.** Forget the rail grid. Park the brush diagonally across three modules. Live your truth.
- **Snap-on-release.** Drop it close to a rail intersection and it commits like a real module, pushing neighbors aside Befaco-style.

<sup>† The bristles push back too, in a manner of speaking — but the cables don't actually move. Yet.</sup>

## Screenshots

| | |
|---|---|
| ![bristles catching on a row of knobs](docs/bristles-on-knobs.png) | ![bristles bending over a tangled patch](docs/bristles-on-cables.png) |
| *Per-bristle penetration response against ParamWidgets.* | *Cable-aware bristles. They know.* |

<!-- TODO: drop two real screenshots in docs/. -->

## Build it yourself

Requires the **VCV Rack 2 SDK** unpacked somewhere on your machine. By default
the Makefile expects it at `~/Rack-SDK`. Set `RACK_DIR` to point elsewhere.

```bash
# Build the plugin.
make RACK_DIR=$HOME/Rack-SDK

# Build and install into VCV Rack's user plugin dir.
make install RACK_DIR=$HOME/Rack-SDK

# Tear down build artifacts.
make clean
```

After `make install`, launch VCV Rack 2 and find the module under the **Synth Duster** brand. Drag it into your patch. Behold.

## Architecture, briefly

| File | Job |
|---|---|
| `src/plugin.{cpp,hpp}` | Standard VCV Rack plugin entry + model registration. |
| `src/Duster.cpp` | The `Module`, the `BristleWidget` (physics + draw), the `DusterWidget` (free drag, snap-on-release, draw-above-cables). |
| `src/Bristles.inl` | Generated. 101 `BristleDef` records — anchor, rest tip, width, colour. |
| `res/Duster_handle.svg` | Renders as the SvgPanel — handle, wood grain, label. |
| `res/Duster_bristles.svg` | Bristle source data. Never rendered at runtime — parsed by the generator. |
| `tools/gen_bristle_data.py` | Reads the bristle SVG, emits `src/Bristles.inl`. |

Tweak the art? Edit `res/Duster_bristles.svg`, re-run `python3 tools/gen_bristle_data.py`, rebuild.

Tweak the feel? Constants at the top of `BristleWidget` in `src/Duster.cpp`:

| Constant | What it does |
|---|---|
| `kStiffness`, `kDamping` | Spring that returns a bristle to its rest position. |
| `kDragGainX`, `kDragGainY` | How much bristles lag when you drag the handle. |
| `kContactDamping` | Velocity multiplier when a bristle hits something. |
| `kCableRadiusPx` | How thick we consider a cable to be for collision. |
| `kCableSagFactor` | How much each cable sags in the obstacle approximation. |
| `kMaxDispMm` | Clamp on bristle displacement so they can't fly off into orbit. |

## Are you ready to buy the real thing?

The pixel brush in your rack is, in the end, made of pixels. The real one is made of actual bristles attached to actual wood, and it actually cleans your actual modules. If your wrist is sore from dragging fake bristles around with a mouse, treat yourself:

- **Official:** [Befaco — Synth Duster](https://www.befaco.org/synth-duster-2/)

Or pick one up from your favorite Eurorack dealer:

- [Perfect Circuit](https://www.perfectcircuit.com/befaco-synth-duster.html) (US)
- [Detroit Modular](https://www.detroitmodular.com/products/befaco-synth-duster) (US)
- [Signal Sounds](https://www.signalsounds.com/befaco-synth-duster-cleaning-brush) (UK)
- [Schneidersladen](https://schneidersladen.de/en/befaco-synth-duster) (DE)
- [Thomann](https://www.thomann.de/de/befaco_synth_duster.htm) (DE)

We get nothing if you click these. We just like our friends at Befaco.

## Disclaimer

Unofficial. Fan-made. Not affiliated with, endorsed by, or financially supported by Befaco. The real Duster is a delightful physical product you should also buy. The author owns one. It is, in fact, dustier than this one.

No actual knobs were brushed in the making of this README.

## License

GPL-3.0-or-later. See `plugin.json` for metadata. Bristle artwork, handle SVG, and physics code are all original to this project.

## Credits

- The original Befaco Duster, for the inspiration.
- Andrew Belt and the VCV team, for shipping a synth host weird enough to host a brush.
- Everyone whose patch is too clean to need this. (Cowards.)
