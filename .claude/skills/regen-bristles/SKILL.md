---
name: regen-bristles
description: Regenerate src/Bristles.inl from res/Duster_bristles.svg after editing the bristle source SVG. Use whenever a bristle's position, colour, width, or count changes in the SVG — the .inl is generated, never hand-edit it.
---

Run from the repo root:

```bash
python3 tools/gen_bristle_data.py
```

The script:

- Parses `res/Duster_bristles.svg` (which holds three `<g id="bristles-*">`
  groups: back, main, highlight).
- For each `<line>`, treats the larger-y endpoint as the anchor (handle
  side) and the smaller-y endpoint as the rest tip.
- Reads the parent `<g>`'s `stroke`, `stroke-width`, and `opacity` for
  colour and line thickness.
- Emits `src/Bristles.inl` with a `kBristles[]` array of `BristleDef`
  records and a `kNumBristles` constant.

After regenerating, **rebuild** so the new data is compiled in. The
generated `.inl` is committed to the repo (so builds don't require
Python at build-time), so include both `res/Duster_bristles.svg` and
`src/Bristles.inl` in any commit that changes bristle data.

Stdlib only — no external Python deps.
