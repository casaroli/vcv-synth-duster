---
name: build
description: Build the Synth Duster VCV Rack plugin against ~/Rack-SDK. Use this whenever a code, SVG, or generated-header change needs to compile, or when verifying that the dylib still builds cleanly.
---

Run from the repo root:

```bash
make RACK_DIR=$HOME/Rack-SDK
```

Produces `plugin.dylib` next to the Makefile. Build time is a few seconds
on warm cache, ~10–15 s cold.

**Expect** clang IDE diagnostics about `'rack.hpp' file not found`,
`Use of undeclared identifier 'config'`, etc. These come from
clangd-in-IDE which doesn't see the SDK include path; the actual `c++`
invocation inside `make` does, and resolves them. If `make` returns 0,
the build succeeded — don't try to "fix" those diagnostics.

If the build fails for real, look near the bottom of `make`'s output for
the first compiler error. Common gotchas:

- A typo referencing `widget::`, `math::`, or `app::` types — those are
  inside `rack::`, but `using namespace rack;` is in `plugin.hpp` so they
  should resolve. If you see `'foo' was not declared` after editing,
  check the include is still `#include "plugin.hpp"`.
- A missing comma in `src/Bristles.inl` after editing the Python tool —
  re-run `python3 tools/gen_bristle_data.py`.
