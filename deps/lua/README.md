# Pinned Lua dependency

`pristine/` is the unmodified 75-file Lua 5.5.1 distribution, copied from the archive with official SHA-256 `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`. Every pristine file and raw upstream patch is pinned in `pins.json`. The MIT license is preserved in source headers, upstream documentation and `LICENSE.txt`.

`tools/prepare_lua.py` verifies the inventory/hashes, copies pristine `src/` into ignored build output and applies only root C-source hunks from the recorded upstream patch files. Every old hunk must match exactly once; no fuzzy application or downloaded build-time input is accepted. Upstream manual/test hunks remain preserved in the raw patches for review but are not applied to the build tree. The selected build copy is recreated deterministically while preserving timestamps of unchanged files. No external patch or Git program is required at build time.

Run:

```sh
python3 tools/prepare_lua.py
python3 tests/mods/check.py --cc clang --sanitize
```

Patch provenance:

- [GC parameter undefined behavior](https://github.com/lua/lua/commit/0b29f408433e92953cc72b1d3e06c7ac8139e439)
- [NaN numeric loops](https://github.com/lua/lua/commit/e8533201cd2448d668c9b64758fc44205544b27f)
- [Negative-zero subtraction](https://github.com/lua/lua/commit/b35736b9d71182bae67492af2a387b3f6e89fd05)
- [Coroutine error cleanup](https://github.com/lua/lua/commit/15b2a78106f85a3f8ef2efe5683148d9dad84b33)

These correspond to all four 5.5.1 issues listed on the [official bug page](https://www.lua.org/bugs.html) when checked November 6, 2026. The [official download page](https://www.lua.org/download.html) identified 5.5.1 as current at that recorded check; this pin is not a claim about later upstream releases. Regression cases in `tests/mods/dependency-regressions.lua` derive from the MIT upstream reports/tests and run in a separate full-library test interpreter, not the restricted game mod profile. The original standalone hardening tests also pass against this patched dependency under sanitizers.

Changing the pin, patch inventory or exposed libraries requires explicit review and rerunning dependency and lifecycle tests. Do not edit pristine files to make patches apply. These fixes do not establish a complete security sandbox or arbitrary crash recovery.
