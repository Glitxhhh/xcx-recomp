# Inline text icons: traced guest route and corrected probe

Trial-only investigation on `investigate/glyph-inline-prompts-local`, baseline `2e3a10697cc5117f1a97f762d38f487594a6c46b`. No replacement is implemented and inline icons remain native. Pinned game: XCX DE 2.0.0, main BuildID `ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a`. Addresses are main-relative.

Evidence is graded, and each statement below says which grade supports it:

- **Static**: decoded from the pinned `main` with capstone by `build/glyph-icon-route-20261109/verify_sites.py` (33 checks, all pass; output in `static-verification.json`).
- **Lifted**: observed by executing the generator's own C for the guest function on synthetic memory (`tests/inline_icon_oracle.py`).
- **Fixture**: synthetic tests that mirror an assumption. They show the probe code is internally consistent; they are **not** proof about the game.
- **Runtime**: observed in a game run. The only run so far used the superseded probe.

## History

The first probe hooked the wrong vtable slot and the wrong draw pass. Its recording (exit 0, 35.76 s, preserved with a manifest at `/var/mnt/game_drive/github/glyph-text-route-recording-20261109T175642Z/`) holds **runtime** facts only about ordinary font binds: 12 events from three fonts (`…d10`, `…dd0`, `…b90`), descriptor 0×0, textures 536×676 (two) and 8244×9136, all format 1, and no layout events. "Resource equals page" compared a value with the expression that produced it and proved nothing. The earlier claim that icon layout leads to a `font_icon` bind is withdrawn.

## Established: layout method (static + lifted)

1. **Address point (static, lifted).** The layouter vtable address point is `0x1c93f08`. `f_7000a204cc` stores `[GOT 0x1d65da0] + 0x10` at `layouter+0`; the GOT slot resolves to `0x1c93ef8` by RELATIVE relocation; the two words below the address point are null. The lifted initialiser, run on a synthetic GOT page, stores exactly that value plus `0x10`. Slots: `+0x50` = `0xa23150` (tag dispatcher), `+0x78` = `0xa21750`, `+0x80` = `0xa21afc`, `+0x88` = `0xa21e94`.
2. **Tag to code (static).** The dispatcher's `ML:icon` branch (string `0x17a0aac`) loads the vptr, calls slot `+0x80` with `x0` = layouter and `x1` = the 32-bit code of the matched 0x24-byte name entry (`{u32 code, …, name[0x14] at +0x10}` in the vector at `layouter->+8->+0x58`). `[ST:icon p1=a ]` text is converted to `[ML:icon …]` by `f_7000912c14`.
3. **Entry hook word (static).** `0xa21afc` is `f9400c0a` = `ldr x10, [x0, #0x18]`: the icon source `X` at `layouter+0x18`. `x1` is masked to 16 bits.
4. **Record lookup (lifted, 48 combinations, 5 mutants rejected).** With source `X`: table pointer `X+0x38` (zero selects a fixed full-texture fallback), record count `X+0x48`, scale floats `X+0x4c/+0x50`, record offset word `table+0x18`. The record index is `code & 0xffff` when that is below the count, otherwise 0. Records are 28 bytes. For every tested code, table offset and layout mode, exactly words 0 to 5 of the selected record change the guest function's output, and word 6 never does. Wrong strides, a `>=` count compare, an 8-bit key, a different offset word and a missing fallback are each caught.
5. **Other fields (lifted).** The run colour bytes come from `layouter+0x68..0x74` times 255. The method emits nothing once `layouter+0x49c` reaches the metrics limit `u16 at metrics+0x1a`, and does nothing without a source.

The lifted function reads the guest GOT, the font descriptor at `font+0x60` and uses `sp` as scratch. The oracle supplies all three and stays UBSan-only because the lifted code addresses fixed guest addresses.

## Established: the two icon-source binds (static only)

`f_7000a322cc` binds the icon source's texture at BL `0xa325f8` and `0xa32688` (words `9400544a`, `94005426`). Decoded set-up at both: `x8 = [x19+0x3b8]`, `x0 = [x8+0x40]`, `x1 = 0`, call `0xa43940` (returns `[x0+0x30] + index*0x78`), `w2 = [x19+0xf2]` (slot byte), `x1 = result`, `x0 = x20` (render context), `w3 = 0`, then `bl 0xa47720`. That helper forwards the entry as first argument and `x0 + slot*32 + 0xe8` as second to `f_7000961224`, which reaches the BindTexture import chain. `x19` is written only by the prologue `mov x19, x0` and epilogue restores in the function body. The decoded sites are the same two on both control-flow paths, so both are wrapped. The glyph-pass binds `0xa32414`, `0xa324c4`, `0xa32540`, `0xa32770` and the `node+0x3c8` bind `0xa32568` are ordinary and are left unwrapped.

The icon source reaches the node as follows (static): `f_7000a1d260` passes `*(*(*0x1d54ba0)+0x58)+0x68` as `f_7000a27920`'s fourth argument, which caches it at record `+0x358` and gives it to `f_7000a204cc`, which stores it at `layouter+0x18`. `f_7000a28358` copies the record to `node+0x398..0x3b8`.

Whether `c->x[19]` really is the node at the wrapped call site is checked **at run time** by the probe: it recomputes entry 0 from `c->x[19]+0x3b8` and compares it with the argument the guest passes. That can only match if the register is right, and the log prints the result for every event.

## Runtime result: first run of the corrected probe (November 9, 2026)

Recording preserved, with manifest, at `/var/mnt/game_drive/github/glyph-icon-route-recording-20261109T190146Z/` (the working copies stay in `build/glyph-icon-route-20261109/`). Exit 0, 178.58 s, 37,387 bytes, no process left. Marks at game.log bytes: `start` 24970, `visible` 27294, `visible` 34715 (the owner opened the page once early, then reopened it), `end` 35492. `analysis.txt` and `uv-check.txt` hold the derived tables.

**Observed (runtime):**

- The layout hook fired for six distinct icon codes, all from **one icon source `X` = `341b36c10`** with record count **48** and scale floats 50×46: `crs` (record 0), `a` (7), `mns_l` (27), `anarp_l` (34), then, after the first `visible` mark, `x` (5) and `crslr` (22). Codes equal record indices, and the tags are `ST:icon` names from the message tables.
- The logged record words are UV rectangles on an **8×6 grid of 46×50-texel cells over 368×300**: for all six, words 2 and 3 are 45.0×49.7 texels and words 4 and 5 are the origin at `(col*46 + 0.5, row*50 + 0.5)` (for example `a`: record 7 = column 7, row 0, origin 322.5, 0.5; `crslr`: record 22 = column 6, row 2, origin 276.5, 100.5). `uv-check.txt` has all six. The role of words 2 to 5 is read from these values; the oracle only established that words 0 to 5 matter.
- Both second-pass bind sites, `0xa325f8` and `0xa32688`, ran for one node `34505d210` whose `node+0x3b8` is that same `X` (seen at layout), with **`x1 == entry 0` recomputed from `c->x[19]` at both**, so the `x19` assumption held. Draw counts: glyph pass 16, second pass 1.
- The registered texture behind that bind is texture 816, handle `10000330`, **368×300, format 77, no view**. Format 77 is the code the accepted HUD atlas path requires for the 512×192 `icon_button` image, which is BC7 offline, so it is consistent with BC7. That equivalence is an inference.
- Call totals: 1000 or more binds (below 10,000) and 10 or more but fewer than 100 layout calls over the run.

**What this establishes:** for this run, an icon laid out through `f_7000a21afc` and the texture bound in the draw's second pass come from the same 48-record source, whose texture is 368×300, matching the offline `font_icon` description, and whose records are cells of that atlas. The contradicting outcomes the probe was built to catch (another size, a source not seen at layout, `x1 != entry0`) did not occur.

**What it does not establish:**

- **The Auto-attacking page specifically.** Events are deduplicated per source/code and per site/source/entry, so repeat uses are invisible: the `a` event (byte 18130) predates the `start` mark, and the two bind events (bytes 21531 and 22025) predate it too. The bind totals show the 1000th bind call at byte 26858, before the first `visible` mark, so these binds come from text on screen from early in the run, not from the Tips page. The only icon events inside the marks are `x` and `crslr`, new codes laid out between the two `visible` marks; the log cannot say which page text they belong to.
- Which in-game text owns node `34505d210`, and whether other nodes use other sources. Only one source appeared.
- Whether a second font-icon variant (the standard and large pair offline) shares this source.
- Refresh behaviour of copied commands, and any replacement design. Nothing here is a replacement contract.

A follow-up that attributes events to a page needs a probe change (for example resetting the de-duplication when a mark is written, or counting each code per phase). That is a new diagnostic and needs review first.

## Not established after the run

- That the Gameplay Tips A icon itself goes through this path (likely, given the tag vocabulary and shared source, but unobserved for that page).
- Whether the second pass draws the layout's runs (draw count 1 with one icon-bearing string is suggestive only).
- Copied-command refresh after a style change.

## The probe (opt-in `XCX_GLYPH_ICON_PROBE=1`, read-only)

`recomp/appendage.py` adds three guarded sites; `runtime/appendage_glyph.c` holds the logging; the record lookup is in `runtime/inline_icon_probe.inc`. Generated guest C differs from the accepted output by one include, one entry hook and two bind wrappers; `recomp/patches-upd.txt` SHA-256 is unchanged.

- **Layout events** at the `0xa21afc` entry: code, key, tag name, source `X`, lookup status (including no source and no table), count, scale, selected index, record address, the seven raw record words and six floats, run tint, `layouter+0x49c`/limit, font, metrics and caller. At most 64 distinct source/code pairs.
- **Bind events** at `0xa325f8` and `0xa32688`: site, node, source `X`, whether that source was seen at layout, source status and header, texture set pointer, recomputed entry 0, the guest's `x1`, `==`/`!=`, slot byte, flag, glyph and second-pass draw counts from the geometry, and the 15 words of the entry. At most 32 distinct site/source/entry triples. Each event arms the existing handle observer, which logs the registered texture's handle, slot, serial, storage, **actual width, height and format** and view as `resource probe 200+n`. A bind that never reaches BindTexture logs that instead. Nothing is filtered on 368×300.
- **Totals** are logged at 1, 10, 100, … calls, so volume stays visible after the caps.
- **Bounds and safety:** 480 s from adapter start, the caps above, every dereference guarded by `plat_mem_readable`, no pointer kept after its call, original execution always runs unchanged with CPU state restored (fixtures compare the whole `Cpu`), and a bind scope the caller had set is restored.
- The superseded probe (vtable slot `+0x78` and glyph-pass binds) is removed, along with its fixtures.

## Checks run

| Check | Result |
| --- | --- |
| `verify_sites.py` against the pinned `main` | 33/33 pass (after correcting two over-literal patterns and a scan range of mine; no code assumption changed) |
| `tests/inline_icon_oracle.py` | 48 combinations pass; 5 deliberately wrong lookups rejected; one gap found and closed (code `0x105` separates 16-bit from 8-bit keys) |
| `tests/appendage_glyph_test.py` (ASan/UBSan) | all modes pass, including `icon` |
| Regeneration | 197/197 chunks, 0 unsupported, `patches-upd.txt` unchanged |
| Generated C diff against the accepted worktree | one include, one hook line, two bind lines |
| Native build, two jobs | 304 objects, 0 errors; probe strings present, superseded strings absent |
| `check_content.py`, `--staged`, `git diff --check` | pass |
| Owner game run | exit 0, 178.58 s; results in the section above |
| Not run | Unicorn differential suite (no lifting changed; Unicorn is not installed here) |

## Owner test (run once; commands kept for review)

Directory: `/var/mnt/game_drive/github/xcx-recomp-glyph-local/build/glyph-icon-route-20261109`. Candidate `run/xcx` SHA-256 `01af1cad43ded75f0a92a505f0895ffb276c6fbf199b41e1832604fa1e2348f5`. Profile: `/var/mnt/game_drive/xcx-prompt-test-profiles/glyph-inline-trial-20261109-03`, a verified copy of the pristine disposable profile (`profile-copy.json`).

```bash
cd /var/mnt/game_drive/github/xcx-recomp-glyph-local/build/glyph-icon-route-20261109
python3 launch.py --check      # no window
python3 launch.py              # game, 540 s limit
python3 mark.py start|visible|end   # from a second terminal, see checklist
python3 control.py dry-run     # lists the exact candidate process, if any
python3 control.py stop        # SIGTERM to that process only
python3 analyze.py             # after the run: facts by phase
```

Checklist: load slot 2, then Main Menu > System > Gameplay Tips and highlight **Auto-attacking** (do not open it yet), run `mark.py start`, open the page, and while the A icon is visibly on screen run `mark.py visible` and wait 10 seconds. Leave the page, run `mark.py end`, close the window normally. Change nothing.

The marks record `game.log` byte offsets, so the analysis can separate icons laid out and bound while the page was open from earlier text. Reading the result:

- A layout event with tag `a` between `start` and `end`, whose source reappears in bind events with `x1 == entry0` and a texture of 368×300: supports the `font_icon` hypothesis for that path.
- Any other size or format on those binds, or a source that was not seen at layout: contradicts it. Report it as found.
- No layout event in that window: the page was not laid out through `f_7000a21afc` then (or was cached from before `start`; reopen it after `start`).
- Layout events but no second-pass bind, or `x1 != entry0` everywhere: the draw-side assumptions are wrong, which is also a result.

A single run proves what happened in that run only. The launcher refuses to run again over its recording; a further run needs a new evidence directory and a reviewed reason.

## Replacement candidate (November 9, 2026)

Status: **implemented and automatically tested; not yet visually accepted by the owner.** It replaces eight ordinary buttons in text lines,
through the shared `ST:icon` source, with no change to the native atlas.

### What it replaces

The 48 records of the shared source are the msbp `icon` enum in order (A=7, B=8, X=5, Y=6, L=13, R=14, ZL=15, ZR=16; the same 24 again as
large variants at +24). Sixteen cells are replaced: those eight actions in the small and the large variant. Every other cell keeps its
native pixels: D-pad, sticks, `+`/`-` (no matching tile), power/home/capture, `anal/anar/analp`, `crs*` compounds.

| Source finding | Rows in the offline message tables (`build/glyph-inline-replace-20261109/all-icon-rows.json`) |
| --- | --- |
| 279 rows carry any `ST:icon`; 218 carry a replaceable button tag | `MNU_CtrlInformation_ms` 86 (menu bottom hints and Control Guide), `MNU_FooterGuide_ms` 56 (large), `MNU_TipsList_ms` 43, `FLD_HintList_ms` 22, `menu_program_ms` 8, `MNU_CommonTelop_ms` 2, `MNU_Option_ms` 1 |
| **Not found as text** | Settings tab L/R: no L/R row exists in the option tables. Whether those tab icons use this path at all is **unverified** (likely screen-layout resident); the owner checklist asks to look. |

Tags are traced to records through the enum and through runtime-captured tag/record pairs (not through appearance); every R tag is a
single record (R = 14, large 38), but the *action* each R stands for differs per sentence, so the replacement follows the physical
button, which is what the glyph policy already does for the HUD route.

### How it works (shared inline path)

1. `f_7000a322cc` binds entry 0 of the source's texture set at `0xa325f8` and `0xa32688` through `f_7000a47720`. The generator wraps those two
   `BL`s (`appendage_glyph_icon_bind`, instruction words verified). The wrapper checks that `x19` is a node whose `+0x3b8` is a source
   with 48 records, scales 50/46 and the SHA-256 of the 28-byte record table (`INLINE_RECORDS_SHA256`), and that `x1` is exactly entry 0
   with the 368×300 / format 0x4d descriptor. Anything else leaves the original path untouched.
2. If the current style replaces any of the eight, the BindTexture import inside that one call returns a tagged handle
   (`0xa6…`: signature id in bits 32..43, original texture/sampler ids in the low 32 bits). Same carrying/resolving contract as the accepted
   HUD handles (`0xa7…`): copied draw commands keep the handle; the resolver reads it at draw time; no guest memory or native atlas is written.
3. The resolver builds, once per (signature, atlas storage), an owned 368×300 RGBA8 image: the native BC7 atlas is decoded by
   `vkCmdBlitImage` on the GPU (no CPU decoder, no pixels redistributed), then `vkCmdCopyBufferToImage` overwrites only the replaced cells
   with the licensed tiles fitted into each cell's native glyph box. Image and view live until shutdown. Failure at any stage returns the
   native texture.
4. Policy is `glyph_decide_inline_set`, the HUD policy applied per action: Original, Automatic (native for Nintendo controllers), PlayStation,
   Xbox, Steam, Keyboard/Mouse, Standard/Colored, Button layout (Nintendo vs Xbox swaps A/B and X/Y), and the current `InputMap`.
   An action that is unbound, bound twice, shared with another action or has no tile stays native; the other seven are unaffected.
5. Refresh: a changed style, binding or layout gives a different signature at the **next** bind, i.e. the next time text is laid out and drawn.
   No rebuild is forced. Text whose draw commands the game caches keeps its handle until that page is rebuilt, so the checklist says to close
   and reopen the menu page after changing a setting.

### Limits (stated, not hidden)

- **Colored**: the tiles are drawn through the text material, which tints by the text colour. Standard tiles have white bodies and are
  expected to look right; Colored may look tinted. Unverified until seen.
- At most 24 distinct signatures per session; beyond that, new combinations stay native.
- The signature fixes the atlas storage at first capture. If the game moves/reloads the atlas (new serial or storage), the handle falls back
  to the native texture.
- The composite snapshots the native atlas when it is built, so the native glyphs beneath the replaced cells are not shown anywhere.
- Opt-out: `XCX_INLINE_GLYPH=0` disables only this path.

### Checks run (this tree, `build/glyph-inline-tests-5/`, `build/glyph-inline-oracle-4.log`)

| Check | Result |
| --- | --- |
| `tests/appendage_glyph_test.py --gpu-build build/glyph-inline-trial-5` | exit 0: policy (14,620 cases), unit enabled/guides/resources/icon/inline/disabled, hook identity, SDL, GPU stages normal/pixels/image/upload/blit/view/source |
| Inline GPU fixture (RX 7900 XTX, real Vulkan) | A synthetic 368×300 BC7 atlas of solid blocks is decoded by the real blit and compared byte for byte with the expected image: every texel outside the replaced cells equals the native decode, every replaced cell equals `glyph_art_inline_cell`, the native BC7 blocks read back unchanged, a second signature gets its own image, an atlas re-registration or foreign storage falls back to native, and every injected failure keeps the native texture |
| Lifted-code oracle (`tests/inline_icon_oracle.py`) | exit 0 against the regenerated guest C; five mutations of the record lookup rejected |
| Source identity | the closed-form record table hashes to `INLINE_RECORDS_SHA256` (checked in Python and in the unit test) |
| `check_content.py`, `--staged`, `git diff --check` | exit 0 |
| **Not covered** | Vulkan validation layer is not installed here (`validation enabled 0`), so zero reported errors says nothing about layer coverage. The fixture uses solid BC7 mode-6 blocks, not the game's atlas; real game use is the owner test. No Unicorn run (lifting unchanged). No Windows build. |

### Owner test

Candidate `build/glyph-inline-replace-20261109/run/xcx`, SHA-256 `efa566bd4cc836a41902b94cf54162f8fa963cfbda25ec52d4a0d82b3f99894b`, built from `generated/upd` with two jobs into
`build/glyph-inline-trial-5`. Profile `/var/mnt/game_drive/xcx-prompt-test-profiles/glyph-inline-replace-20261109-01` (a fresh copy of the pristine
`glyph-actionable-20261109-01`; all 46 recorded files verified). Commands, locations and the comparison checklist are in
`build/glyph-inline-replace-20261109/OWNER-CHECKLIST.md`; the launcher refuses a profile that no longer matches `profile-copy.json`.

### Owner observations and remaining limits (November 9, 2026)

Two owner sessions with the candidate. These are owner-reported screenshots, not independent traces.

- **Observed working:** inline buttons in Gameplay Tips text, a field hint pop-up (`L + right stick`), and the Map/Segments footer (`ZL` tiles, `Y`, `X`), with PlayStation and Xbox styles, Button layout changes and Standard vs Colored. Colored face tiles rendered at full colour with no visible tint on the Map footer. The `X Access` field prompt (accepted HUD route) still works.
- **Native, layout-based prompts (not covered):** tab `L`/`R` icons (Map, Tips), page-turn `R` icons, `Details ... ZR`, the popup `A Close` (Story Mission Complete, tip popups), `X Check Control Guide`, the `R + D-pad` mini-map widget. No `Close` + A message row exists, which fits layout-resident widgets; the route into their textures is not traced.
- **Native on purpose:** stick-presses (`Find Self`, `Show Navigation`), D-pad, `+`/`-`.
- **Not yet accepted:** Keyboard & Mouse and Automatic with inline text, and whether a page reopen is needed after a style change. Acceptance of the whole candidate is pending the source review.

## Layout buttons, D-pad, sticks, plus/minus (second pass, November 9, 2026)

Status: **implemented and automatically tested; not yet visually accepted.** The first pass left the layout-drawn prompts native
(tab L/R, page-turn R, popup `A Close`, `Details ZR`, `Check Control Guide`). This pass traced them and extended the same
mechanism; it also added the D-pad, stick and plus/minus symbols to the text path.

### Source findings (offline, from the pinned archive; `build/glyph-layout-trace-20261109/`)

- The game's `sts.ard` has 104,824 members addressed by `xxh64` of the path. The reader was checked against the recorded `font_icon`
  SHA-256. 43 layouts reference the image **`/ui/image/icon_button.wilay`**: the page-turn (`common_pageline`), tab
  (`common_fieldtab`, `mc_IconLButton/RButton`), pager, guide, `common_button` and shoulder-button layouts among them. It is a
  **512x192 BC7 sheet of 24 cells of 64x64**, the same 24 symbols in the same order as the text atlas (`crs pow hom mns pls x y a b
  analp anarp anal anar l r zl zr crsu crsd crsl crsr crsud crslr cap`): the 512x192 image the accepted HUD route already
  swaps for its guide nodes. Its 24-record table is a closed form; its `sha256` (`8c6c9783...`) is the new identity constant, and records
  14, 6, 7 / 5 / 15 of the closed form reproduce the three hashes the HUD route verified on real runs, so file records equal the live resource.
- The first 128 bytes of the image file hash to the HUD route's runtime guard (`ed9164c1...`), i.e. the live resource object equals the file
  header. The same relation holds for the other two images' probe constants.
- **`icon_button02`** (352x88, 4 cells of 88x88: left/right stick vertical and horizontal; referenced by one layout, the Control Guide
  "operate" pages) and **`icon_button03`** (368x150, a white-mask copy of the 24 symbols, referenced by one layout with `rect_Btn01/02`).
- The shared type-1 rectangle draw (`0xa275c0`, the loop whose bind the HUD route already hooks at `0xa27678`) binds `table[frame]` once
  per layer for every image pane, so these panes reach the existing hook; no new generator site is needed.
- Not on this path: `common_button_red_a` (a single 64x64 red-ringed `A` image in `common_button`), `03020_button_glow`, the Soul Challenge
  `B`, `icon_button03`'s consumer. They are listed, not claimed.

### What was added

1. **One symbol table.** `SYM_*` (24 shared symbols plus the four stick-direction symbols) and `glyph_decide_symbol_set` give one policy
   for the text atlas, the sheet and the stick image: single-action symbols follow the current binding (Button layout, rebinding, unbound
   or shared inputs fall back per symbol); the whole D-pad, vertical and horizontal pairs draw a group tile only when the actions are
   bound exactly to the matching D-pad inputs (any permutation), otherwise native; the sticks as axes are fixed on a pad (not rebindable).
   Keyboard & Mouse uses the bound keys (buttons, +/- Tab/M, presses Ctrl/C, D-pad 1-4). Sticks and D-pad groups use a key-cluster tile (arrow keys, WASD, 1-4; four keys, or the up/down or left/right pair) when the directions they stand for are bound, once each and unshared, to the keys of one cluster in the cluster's own positions; any other binding keeps them native. The nine cluster tiles are composed by `tools/make_keyboard_clusters.py` from pinned CC0 Kenney key tiles (members and hashes in the provenance). Art ids are 16-bit; the HUD route addresses only the first 128 tiles.
2. **53 more licensed tiles** from the same pinned Kenney package (CC0), 128 in total: plus/minus, D-pad (whole, four directions, pairs),
   sticks, stick presses and stick directions for PlayStation, Xbox and Steam (the Steam set uses the Steam Deck stick tiles), Tab and Ctrl keys.
   PlayStation plus/minus are the PS5 options/create tiles: the PS4 options and share pills are indistinguishable at 30 px.
3. **Sheet and stick-image arming** in the existing type-1 bind wrapper (`sheet_arm`): any pane whose resource object matches the image's
   guard hash and whose record table matches its closed-form SHA-256 (cached per address and content) tags the BindTexture handle with a
   signature, the same `0xa6` tag, carrying and resolving as the text atlas. A pane the HUD route owns is left to it. With Original selected
   nothing runs. The pane's selector and frame count are only logged (distinct pairs, once each), not trusted: the composite holds every
   replaced cell, so a pane showing any cell uses it correctly.
4. **Three layouts in one composite builder:** BC7 blit decode on the GPU, then only the replaceable cells overwritten (signature per layout and
   per-layout symbol set, so an unrelated binding does not mint a new signature). 48 signatures per session.

### Checks run (`build/glyph-inline-tests-7/`)

| Check | Result |
| --- | --- |
| `tests/appendage_glyph_test.py --gpu-build build/glyph-inline-trial-6` | exit 0: policy 24,849 cases, unit enabled/guides/resources/icon/inline/disabled, hook identity, SDL, GPU stages normal/pixels/image/upload/blit/view/source |
| GPU fixture (RX 7900 XTX, real Vulkan) | for the 368x300 atlas, the 512x192 sheet and the 352x88 stick image: native BC7 decode everywhere outside the replaced cells, replaced cells equal the fitted tiles, native blocks unchanged, second signature separate, re-registration/foreign storage and every injected failure fall back |
| Closed forms | sheet and stick tables reproduce their SHA-256 constants; sheet records reproduce the three HUD runtime hashes; stick guard equals the probe's constant; all glyph boxes lie in their cells; symbol order identical in both headers |
| Unit | per-symbol policy for PlayStation/Xbox/Steam/keyboard, rebinding, shared and unbound inputs, group rules; pane arming incl. wrong class/type/hash/table/texture, cache invalidation, scope isolation; the test memory probe now faults on protected pages like the runtime |
| Mutation spot checks | changed expectations made two of the new assertions fail, so they execute |
| `check_content.py`, `git diff --check` | exit 0 |
| **Not covered** | validation layer not installed; solid-block fixtures, not the game's own images; no pane has yet been seen drawing the sheet in the real game (this is the owner test); `icon_button03`, `common_button_red_a` untouched |

### Owner test

Candidate `build/glyph-layout-candidate-20261109/run/xcx`, SHA-256 `5e8fb9589ceb77c41ad31adcc6edec2f8e83e2383eee5b5b832fe0b7a9349159`, built from `generated/upd` (unchanged) with two jobs
into `build/glyph-inline-trial-6`. Isolated profile `glyph-layout-20261109-01`, a copy of the played disposable profile. Commands and the
comparison checklist: `build/glyph-layout-candidate-20261109/OWNER-CHECKLIST.md`.

## Mini-map `R + D-pad` hint: the baked `01020_guide` image — trial-14

**Finding.** An earlier guess (a white-mask image, `icon_button03`, drawn through the type-1 bind) was reached by no pane in the owner's session, and a census of every distinct image that bind sees (11 of them, none 204x176 and none 368x150) showed the hint does not draw through that bind; the guess and the census mode were removed in trial-27. The hint is the single image `/ui/image/01020_guide` (layout `01020`, the mini-map): 204x176, RGBA8 (NVN format 37), the whole picture baked in (Switch `R` key, plus sign, four-button D-pad, teal; found offline by decoding the member).
**Implementation.** `vk_appendage_texture_locked` (the one place draws resolve a texture handle) recognizes the image by content: a plain handle whose registered texture is exactly 204x176 RGBA8, no view, and whose 294912 bytes of memory hash to the pinned SHA-256 (`HINT_NATIVE_SHA256`, computed from the member's image data). A texture is judged per (serial, address), at most three tries, and the log line `a 204x176 RGBA8 image (texture N) is / is not the mini-map hint (content hash ...)` says why the native hint stayed. When it is the hint and the style replaces `R` or the D-pad, the handle gets the inline tag of a fifth layout (`INLINE_LAYOUT_HINT`) and the existing machinery builds one owned image per signature: the native RGBA8 image is blitted, then two cells are replaced: the `R` key (30x30 at 68,74) and the D-pad (32x32 at 106,72); the plus sign and the rest stay native. The hint is judged by content, not by the source/serial pairing the atlas layouts use, because the layout may recreate the texture. Glyph tiles are the style's `R` and whole-D-pad symbols (keyboard: the bound R key and the 1-4 cluster).
**Owner result on trial-14.** The content hash matched in the real game (`29f7d406ca7a65d9`, try 1) and the hint showed R1 + D-pad, but every style kept the first one: the draw memos (`tex_info`, keyed by the unchanging plain handle and the slot's token) never looked the handle up again after a style change, although the log shows signatures for other styles being created. Trial-15: the hint slot's token (`vk_texture_slot_token`) carries an epoch that moves when the style gives the hint another signature (checked in `vk_appendage_texture_locked`, which every submission calls once per memoized handle), and a pad style draws its lit-down D-pad tile (`crsd`) in the D-pad cell like the native picture; the keyboard keeps the 1-4 cluster.
**Round buttons — trial-26.** The idle button of the field-action prompt is drawn by the `icon_button` sheet route (log: `a layout pane draws the button sheet (selector 7 of 24 frames)`), which fits a tile into the native glyph box of the cell: for the A cell a 56x56 square, the bounding box of a round button, so the corners of a square keyboard key left the circle. For the face-button cells (X, Y, A, B) `glyph_art_cell_round` fits the tile as `glyph_art_cell` does, measures the farthest opaque texel from the box center, and, when the tile is not round (more than 1.12 times the radius of a disc with its area) and that distance exceeds 0.9 of the circle radius, refits it into a box shrunk about the same center. Round tiles (Xbox, PlayStation, Steam faces) are unchanged. (Resizing the HUD route's own tile image, tried in trials 22-25, had no effect on this button and was removed.)
**Held field-action button — trial-21.** While the button of a field action (`Treasure`, ...) is held the layout shows `/ui/image/03020_button_hold` (64x64 BC7, teal ring, dark teal disc, white baked `A`), which the HUD-guide route never touched, so the Switch `A` came back as the gauge filled. It is recognized by content like the other plain images (`plain_kinds`, SHA-256 of 8192 texture bytes) and replaced by a drawn ring and disc (`glyph_art_hold_button`: ring RGBA 0,166,166,254, disc 0,88,96,207, radius 24, soft shadow) holding the style's `A` tile (box 17,17,30x30). The hold circle (`03020_button_hold_circle`) and glow images have no letter and are left native.
**Soul Challenge B — trial-20.** The `B` in the battle's orange ring is the image `02110_button` (96x96 RGBA8; a white disc at texels 8..87 with the letter as a transparent hole; the layout tints it and draws the orange ring behind), not the ring image `02110_judgering_btn`. It uses the same content-recognized route as the mini-map hint (`plain_kinds`: size, format and SHA-256 of 69632 texture bytes), as layout `INLINE_LAYOUT_SOUL` with one 96x96 cell holding the style's `B` tile run through `glyph_art_to_mask` (box 8,8,80x80), and the hint's per-slot token epoch so a style change reaches the memoized draw.
**Trial-19 (supersedes trial-17).** A flat fill still left a faint rectangle: the native halo is a blurred shadow (RGB 14,15,23; alpha 115 next to a drawing, 82 at 3 texels, 61 at 5, 47 at 7, 38 at 9, 24 at 13, 13 at 21). `glyph_art_shadowed` draws the same shadow around the fitted glyph (distance to the glyph, interpolated through those measured points) and composites the glyph over it, in cells that reach about 20 texels past the glyph (R 46x65 at 52,56; D-pad 45x65 at 106,56; glyph boxes 26x26) so the shadow has faded to the native level at the cell border.
**Trial-17 (supersedes trial-16's cells).** Clearing the whole halo made the square larger: the visible edge was the border of a cleared cell. Native texels are RGBA 14,15,23,23 in the halo (and RGB 14,15,23 with alpha 0 elsewhere). Each replaced cell is now filled with the halo texel and the tile composited over it (`glyph_art_over`), with the original small cells (R 30x30 at 68,74; D-pad 32x32 at 106,72).
**Trial-16.** The owner saw a faint square behind every replaced picture: the native image has a soft dark halo behind both drawings (alpha above 16 over x 54..143, y 58..118) and the 30x30 / 32x32 cells cut it off at their edges. The cells now cover the halo of their side (44x61 at 54,58 and 38x61 at 106,58; glyph boxes centered on the old drawings); only the plus sign's column (x 98..105) stays native.
**Checks.** Real-GPU readback (RX 7900 XTX, no validation layer): the composite equals the native image everywhere except the two cells, the native image is untouched, Original and a same-shaped texture with other content stay native, a changed binding gets its own image. Not confirmed on a real game session: the pinned hash against the real texture memory (the log line above settles it) and how the white tiles look beside the teal plus sign.

## Keyboard keys in the glyph translation — trial-9/12

`input_glyph_snapshot` passes each key through `ikb_glyph_key`, which only knew letters, digits, mouse buttons and Space and returned "unsupported" for everything else, so Tab, Shift, Ctrl and the arrow keys never reached the policy in a real game (the unit tests use the raw map). Tab, Shift, Ctrl and the arrows now keep their identity in `runtime/input_sdl.c` and `runtime/input_win.c` (the Windows copy is untested). A Shift tile (`keyboard_shift.png`, Kenney CC0) is the 129th tile, so art id 128: the HUD route's handle names its art in 7 bits, and `appendage_glyph_handle` now refuses a HUD art id of 128 or more (native handle) instead of overflowing into the tag.
