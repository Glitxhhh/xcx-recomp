# Actionable prompt resources — November 9, 2026

The next shared-path target is inline text icons used by HUD hints, menu footers and tutorials. This pass identifies their message/tag/resource family and records a bounded read-only diagnostic. **It adds no new replacement coverage.** Appendage and weapon acceptance remains valid; NPC interaction field A is owner-accepted; other field-A captions, Target R and Board ZL remain visually unconfirmed. See [coverage](prompt-coverage.md) and [review](input-glyph-review.md).

## Verified offline findings

All game-derived material is local and ignored under `build/glyph-actionable-20261109/`. The main identity and thirteen generator guards remain unchanged. Upstream master was fetched and remains `feb125c`; reviewed merge `3896b5b` and the existing patch table were preserved. Before editing, all 36 dirty/untracked files were copied and hash-verified into `preserved-source/`, recorded by `before-manifest.json`.

The original message parser was extended locally to inspect all tables in the already extracted English message archive, rather than only `menu_program_ms`. Bounded table/column/stride/string-pool checks identify:

| Visible target | Table and row | Encoded button |
| --- | --- | --- |
| Hint: Main Menu | `FLD_HintList_ms`, 3 / `HINT_CONTENT_002` | `ST:icon`, `p1=pls` |
| Settings: Revert to Initial Settings | `MNU_FooterGuide_ms`, 85 / `FooterGuide_85` | `ST:icon`, `p1=x_l` |
| Gameplay Tips: Auto-attacking | `MNU_TipsList_ms`, 150 / `TIPS_BSC_015_01_PAGE01` | `ST:icon`, `p1=a` |

These correspond to the owner's prior screenshots; they are offline identities, not a new live trace. The same tag family appears throughout `MNU_CtrlInformation_ms` (114 rows), `MNU_FooterGuide_ms` (105), `MNU_TipsList_ms` (1,268), `FLD_HintList_ms` (80), and other messages. Row counts are table sizes, not implemented coverage or a count of actionable prompts.

`MNU_ResCategory` identifies font-icon images under `ui/image/` with `.wilay`; `MNU_ResImage` row 2 is `font_icon`. The verified archive contains `/ui/image/font_icon.wilay`: 368×300 BC7, 48 atlas records in an 8×6 layout, with standard and large light/white-mask variants. Offline decode on a dark background resolves the buttons. Its full member SHA-256 is `f5eace8510dbcb4bc69f8eeb56c459841653205ff2a2000821a230e77c83ca48`.

The separately extracted `/ui/msproj/sts.msbp` contains the `ST` group, `icon` tag, `p1` parameter and contiguous icon enums `crs` through `cap`, followed by corresponding `_l` variants. Font records 4/5/6/7/8 depict +/X/Y/A/B; 13/14/15/16 depict L/R/ZL/ZR; large variants are 24 records later. This establishes tag/artwork correspondence; it does not identify the live text draw/material call or prove input consumption. Current host binding translation must still be applied per virtual input, with unsupported directions/chords retaining originals. A fixed family image swap cannot represent arbitrary remaps.

## Soul Challenge and excluded leads

Construction `main+0x5c56c4`, invoked from `+0x787744`, resolves `mc_Challenge`, result/ring/text controls, and configures the challenge component. `/ui/image/02110_buttonchallenge.wilay` is a LAGP layout. Its image properties reference `judgering_btn`, resolved through the archive's `/ui/image/02110_judgering_btn.wilay`. The latter is 168×168 RGBA8, SHA-256 `b9a3f24d6e65873cd16f9b10c826185358e97f316109c401890f560cfd962e43`.

**The decoded judgering button image is only the glowing circular background. It contains no B.** Replacing that image would alter the ring and leave the actual button label untouched. The B node/text producer and its input-consumption connection remain unresolved. No ring replacement is implemented. The label `button` occurs in the layout, but its actual resolved node/type is not established by the string alone.

Other excluded selector candidates:

- `0x674310` and `0x689a98`: `mc_IconQuest/rect_IconQuest`, not + button guides.
- `0x5e139c`: sort/filter icons, not controller buttons.
- `0x5a4858`: `rect_IconArtsCaution`, not +.
- `0x5d9ca4`: its selector-4 target is `rect_IconSystem`, not an established + action.
- `0x591438`: item-skill upgrade-specific construction; it is not a universal footer renderer. `MNU_IconList` / `MNU_IconTexList` references `common_bc3_02`, which is absent from the pinned archive under that path. Those legacy table entries must not be treated as proof of the live font-icon path.
- `0x61fd78` sets equipment-part Confirm/Cancel guides. No speculative adapter was added: caption and selector identity alone do not establish its live type/material/input contract.

The current 512×192 private atlases cannot serve the 368×300 font resource unchanged. Their five populated cells and transparent remainder would erase unrelated cells. Font tint/mask shader behavior, copied text-command selection/refresh and resource retirement must be verified before adding another bounded owned-image variant. No renderer expansion or global atlas mutation was attempted.

## Implemented diagnostic and what it can establish

`runtime/appendage_glyph.c` adds `XCX_GLYPH_RESOURCE_PROBE=1`. It uses the **existing verified type-1 bind hook** at `main+0xa27678`, accepts only type 1/vtable `+0x1c94318`, and compares bounded 128-byte resource hashes against four inspected members: `font_icon`, `02110_judgering_btn`, `icon_button02`, `icon_button03`.

It samples at most eight binds per resource, 32 total, and expires 180 seconds after initialization. Each sample copies node/type/active selector/count and the actual texture-object argument. At the existing original BindTexture import return `+0x95c9cc`, a matching same-Cpu call copies the unmodified original handle and the registered host descriptor's size/format/view/serial/storage identity under the existing texture lock. At most 64 observation lines plus the ready line are emitted. The 240-second/32 MiB launcher budget is separate. Unrelated events do not consume matching-resource budgets.

This can establish whether an inspected resource reaches this type-1/material/import route and its concrete registered texture descriptor. It cannot establish a text route that bypasses this hook, caption-to-node correspondence from a resource match alone, Soul B consumption, complete destruction/reconstruction, static text refresh, or GPU fence lifetime. A missing observation only means no matching bind was captured within this route and budget. It is not evidence that an atlas or prompt is absent.

No node is attached for diagnostic purposes. No guest pointer is retained beyond the original bind scope; nested scopes restore prior state. The observer allocates/uploads/releases no texture, changes no handle, writes no guest state/input/binding/visibility and never calls a dirty setter. Normal feature selection and fallback remain unchanged. No new generator sites or generated-C edits were needed.

## Checks and candidate

- ASan/UBSan fixture: unrelated resource/type rejection, unchanged Cpu/handle, four independent eight-sample caps, 180-second expiry and cleared post-call scope pass. Existing retirement/reconstruction/address-reuse and concurrent-node fixtures pass.
- 14,620 policy/action/binding/intent/pixel checks pass. All 75 licensed PNGs and original license match the pinned Kenney 1.5A archive; both derivatives reproduce. No assets were added or regenerated in place.
- Linux native build at two jobs passes; 304 object hashes verified, zero mismatches. Headless selftest passes with zero failures. No generator change requires a new full generation/differential run in this pass.
- Content/staged-content and whitespace checks pass. No I/O errors recur. Initial fixture invocations used a Python without `compression.zstd` or a cached Python-3.14 Pillow; their setup failures are preserved, then the explicit Python-3.14 environment passes. They were not runtime/game crashes.
- The coordinated NPC/menu run closed normally, exit 0, after 186.08 seconds with 39,699 log bytes. The ready line is present and matching resource samples total zero. No GPU fixture or native Windows test was run in this diagnostic pass. Existing GPU evidence remains historical; validation layers remain unavailable.

Frozen executable: `build/glyph-actionable-20261109/run/xcx`, SHA-256 `2858fd455c90393ed8c080b820cec7aec7714a4f571bc766807e44ceb68d3797`.
Disposable profile: `/var/mnt/game_drive/xcx-prompt-test-profiles/glyph-actionable-20261109-01` (46 files copied and hash-verified from the preceding disposable profile). Original profiles were not changed. The old candidate and recordings remain preserved.

Reproduce CPU checks from the glyph worktree:

```bash
uv run --offline --python /usr/bin/python3 --with pillow python tests/appendage_glyph_test.py --cc build/pr118-clang --out build/glyph-actionable-check-new
python3 tools/check_content.py
python3 tools/check_content.py --staged
git diff --check
```

The compiler wrapper uses the existing Fedora-Tools container. Exact native build command and prerequisites remain in [the maintained context](../../xcx-seamless-coop/docs/controls-context.md#candidate-commands-and-evidence); this runtime-only change needs rebuilding, not regeneration. Use two jobs maximum. Evidence: `build/glyph-actionable-20261109/{fixtures-bounded.log,fixtures-bounded/,native-build-final.log,selftest-final.log,asset-verification.json,probe-resource-identities.json,text-inventory.json,tag-enums.json,identity.json,profile-copy.json}`. The inspection scripts/source excerpts and extracted resources remain ignored beside them.

## Completed coordinated owner sequence

This is a route diagnostic, **not a test that all icons now swap**. Accepted Appendage/weapon appearance need not be repeated. The newly investigated menu/tutorial/QTE/+ prompts are expected to remain native.

```bash
python3 /var/mnt/game_drive/github/xcx-recomp-input-glyph-styles/build/glyph-actionable-20261109/launch.py --check
python3 /var/mnt/game_drive/github/xcx-recomp-input-glyph-styles/build/glyph-actionable-20261109/launch.py
```

These are preserved historical commands for the completed run. The one-shot launcher refuses to overwrite its recording. No repeat run is requested. Capture was prearmed; no second chat marker was required.

Load slot 2, approach a nearby NPC without completing dialogue, inspect Talk A/Cross and move away/back. Open in-game Settings → Game and Gameplay Tips → Auto-attacking to expose the native X/A/L/R icons; do not reset settings. Inspect a + Main Menu hint if shown. Soul Challenge is optional only if it occurs naturally; no extended combat, QTE farming, saving or progression is requested. Skip Board without an already available Skell. Target R remains pending if not immediately visible. Close normally with the window close button.

Path/profile-specific emergency stop:

```bash
python3 /var/mnt/game_drive/github/xcx-recomp-input-glyph-styles/build/glyph-actionable-20261109/control.py dry-run
python3 /var/mnt/game_drive/github/xcx-recomp-input-glyph-styles/build/glyph-actionable-20261109/control.py stop
```

The exact-candidate scan is empty; no owned build/test/game process remains. Fedora-Tools is shared and stays running. The one-shot launcher refuses existing game.log; preserve each recording rather than reusing it.

## Optional controller diagrams

The pinned Kenney package includes controller silhouettes for PlayStation 1–5, Xbox 360/One/Series, Steam Controller and Nintendo families, with raster/vector members. Package CC0 licensing was verified; that does **not** establish trademark clearance. No legal clearance or technical fit is claimed.

Native diagram callouts, positions, labels and rebound inputs would need a separate verified page/layout contract; a silhouette swap alone would not make the callouts accurate. Preserve native pages. This enhancement is deferred and is not a gate for ordinary button coverage.


## Owner NPC/menu check — November 9, 2026

The owner reports that approaching NPCs displays the interaction prompt correctly and changing Button layout updates it appropriately. This accepts the NPC interaction instance of field A; it does not accept every field-A caption, Target R or Board ZL. Agent logs independently show slot 4, active selector 7, drawing original and PlayStation Cross/Xbox A/Steam A variants. The supplied Gameplay Tips screenshot still shows native X, L/R and inline A while the host Controls overlay shows PlayStation/Colored selected. This is expected unresolved menu/tutorial coverage, not a failure of accepted HUD coverage.

The completed recording contains the ready line and zero matching resource-probe samples. Normal window close and exit 0 are verified: 186.08 seconds, 39,699 log bytes. The exact-candidate process scan is empty. Hashes and results are preserved in ignored `build/glyph-actionable-20261109/completed-run-manifest.json`. A lack of samples on the scoped type-1 path does not identify the text draw route or prove resource absence. Do not repeat this diagnostic merely to reconfirm native menu icons.
