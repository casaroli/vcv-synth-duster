---
name: install
description: Build, package, and install the Synth Duster plugin into the local VCV Rack 2 user plugin directory so it can be tested in the running app. Use after a build to actually see the change in Rack.
---

Run from the repo root:

```bash
make install RACK_DIR=$HOME/Rack-SDK
```

This:

1. Compiles (same as `make`).
2. Creates `dist/Duster/` with the dylib + `res/` + `plugin.json`.
3. Packages it as `dist/Duster-2.0.0-mac-arm64.vcvplugin` (zstd-compressed tar).
4. Copies the `.vcvplugin` into `~/Library/Application Support/Rack2/plugins-mac-arm64/`.

The user needs to **restart VCV Rack 2** to pick up the new plugin
binary. The module shows up under the **Synth Duster** brand in the
module browser.

For iterative testing, after `make install` tell the user to restart
Rack and try the specific behaviour the change was supposed to affect
(drag, mode switch, cable contact, etc.). Don't claim a change "works"
without the user confirming visually — neither tests nor a successful
build verify UX.
