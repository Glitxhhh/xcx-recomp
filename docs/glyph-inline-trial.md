# Inline-prompt trial worktree

This trial starts from the reviewed `feature/input-glyph-styles` baseline on branch `investigate/glyph-inline-prompts-local`, at `/var/mnt/game_drive/github/xcx-recomp-glyph-local`. The exact baseline and remote hash are maintained in [controls-context.md](../../xcx-seamless-coop/docs/controls-context.md); the existing [shared task list](../../xcx-seamless-coop/docs/recomp-backlog.md) remains the task list.

Appendage, weapon styles and the NPC interaction field-A instance are owner-accepted. Other field-A captions, Target R and Board ZL remain visually unconfirmed. HUD + hints, Settings reset X, tutorial inline icons and Soul Challenge B remain native. The completed resource probe captured no matching samples on its scoped type-1 route. Continue from [the resource report](prompt-actionable-resources.md); do not repeat accepted gameplay checks or the same empty probe.

Trial finding (corrected after the first owner run): `[ST:icon p1=…]` is converted to `[ML:icon …]`, which the text layouter handles through `f_7000a21afc`. The first `XCX_GLYPH_TEXT_PROBE` hooked the wrong vtable slot and the glyph pass and is removed. A corrected read-only probe, `XCX_GLYPH_ICON_PROBE=1`, ran once: layout, draw binds and a 368×300 texture share one 48-record icon source (runtime results in the report below); the Auto-attacking page itself is not isolated. The route (separating static, lifted-code and runtime evidence), the probe's bounds and the owner checklist are in [the inline text route report](glyph-inline-text-route.md).

## Local build prerequisites

The trial checkout contains source, licensed Kenney tiles and reproducible licensed derivatives. It does not contain ExeFS/RomFS, extracted atlases, generated guest C, binaries, profiles, old diagnostic scripts or recordings. Those remain ignored in the accepted feature/recovery worktrees; consult them read-only without overwriting evidence.

- Read `AGENTS.md`, [building](building.md), [glyph contracts](input-glyph-styles.md) and the current shared controls context before work.
- The pinned verified game input is XCX DE 2.0.0. Existing local inputs are `/var/mnt/game_drive/github/xcx-recomp-coop-test-20261105/build/extracted/upd/exefs` and the sibling `romfs` directory. Verify against `tools/data/expected-xcx-2.0.0.json`. Never put these inputs or generated output in Git.
- The existing Fedora-Tools container has the native compiler/build libraries and generator dependencies. The trial directory must be visible at the same absolute path inside it. Leave the shared container and unrelated work running.
- Python 3.14 with Pillow is needed by licensed-asset reproduction (`compression.zstd`). The existing CPU fixtures also need Clang, zstd, sanitizer libraries and platform headers. The generator needs its dependencies listed in the building guide. The existing host command `uv run --offline --python /usr/bin/python3 --with pillow ...` uses the cached environment; a fresh machine must install those prerequisites first.
- Preserve `recomp/patches-upd.txt`; the reviewed merged SHA-256 is `7e9db23915ce32b390b3a4797cc15a6eceba52f7ec0185472c27a2a18c25e8c1`. Regenerate for the trial checkout; do not hand-edit or commit generated C.

From the host, after confirming the trial path is visible inside Fedora-Tools:

```bash
podman exec -w /var/mnt/game_drive/github/xcx-recomp-glyph-local Fedora-Tools python3 recomp/gen.py --exefs /var/mnt/game_drive/github/xcx-recomp-coop-test-20261105/build/extracted/upd/exefs --out generated/upd --patches recomp/patches-upd.txt --jobs 2
podman exec -w /var/mnt/game_drive/github/xcx-recomp-glyph-local Fedora-Tools python3 tools/build_game.py --version upd --gen generated/upd --out build/glyph-inline-trial --exefs /var/mnt/game_drive/github/xcx-recomp-coop-test-20261105/build/extracted/upd/exefs --romfs /var/mnt/game_drive/github/xcx-recomp-coop-test-20261105/build/extracted/upd/romfs --expected tools/data/expected-xcx-2.0.0.json --profile /var/mnt/game_drive/xcx-prompt-test-profiles/glyph-inline-trial-20261109-01 --jobs 2
```

These commands are prerequisites, not a build performed when creating the worktree. Keep one heavy job at a time, at most two compiler jobs. Before any game/GPU session, coordinate with the owner and prepare a separate disposable profile and one-shot launcher. The example build's profile path must be populated from a disposable copy before launching; do not use an owner profile. Preserve the accepted candidate and its original launch/profile paths.

For CPU checks from the trial checkout, create a local ignored compiler wrapper:

```bash
mkdir -p build
cat > build/trial-clang <<'SH'
#!/bin/sh
exec podman exec -w /var/mnt/game_drive/github/xcx-recomp-glyph-local Fedora-Tools clang "$@"
SH
chmod +x build/trial-clang
uv run --offline --python /usr/bin/python3 --with pillow python tests/appendage_glyph_test.py --cc build/trial-clang --out build/glyph-inline-tests
python3 tools/check_content.py
python3 tools/check_content.py --staged
git diff --check
```

This wrapper is local build setup, not a new tracked file. Keep source changes on the trial branch; do not modify or copy unrelated Lua, Online or naming work.
