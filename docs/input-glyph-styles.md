# Experimental HUD input glyph styles

This adapter changes selected HUD button images for the verified XCX DE 2.0.0 build. The three-button enemy-targeting **Appendage** guide passed Linux owner acceptance. The shared weapon-change X also passed the Linux owner check. The added field A-button guide nodes passed live type/resource guards, but their visible caption/artwork acceptance remains pending. See the [coverage inventory](prompt-coverage.md) for evidence and exclusions. Original rendering is the default. Players choose the style under F1/Esc → Controls; see [Controls](controls.md#experimental-appendage-glyphs) for use and fallback behavior. HUD paths outside that inventory, menus, tutorials and QTE prompts remain unchanged.

This page is the developer reference. [Review evidence and checklist](input-glyph-review.md) separates source contracts, automated checks and Linux owner acceptance. Native Windows and Vulkan validation-layer acceptance remain unverified. Standard and Colored face buttons are separate from device style.

## Supported build and insertion points

| Identity | Value |
| --- | --- |
| Main NSO SHA-256 | `0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026` |
| Main build ID | `ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a` |
| Main module base | `0x7000000000` |
| Component | `hud_PartSelection01` / Appendage, `mc_Top/mc_Guide` |
| Nodes | Three type-1 `rect_Btn` nodes, verified vtable `main+0x1c94318` |
| Resource | Verified `icon_button.wilay` prefix and selected-record hashes |

`recomp/appendage.py` verifies the source hash, build ID, module base and all thirteen instruction words before emitting adapters. Unsupported build IDs receive no adapters; identity/instruction mismatch for the supported build stops generation. Controls is disabled for an unsupported baked game identity.

| Main offset | Adapter |
| --- | --- |
| `0x60f68c`, `0x60f6a8`, `0x60f6c4` | Observe the actual resolved R/Y/A wrappers before the original selector calls |
| `0x55ff1c` | Observe the resolved rectangle; admit only verified parent return addresses from the inspected saved frame |
| `0xa27678` | Scope the selected live node while preserving the original bind call |
| `0xa2fac0`, `0xa2fcd0` | Retire the node before destruction or initialization |

The runtime also checks the live node type/vtable, 24-record count, resource prefix, record bounds and selected-record hashes. Selectors 14/6/7 identify R/Y/A artwork, not host action IDs. All three Appendage nodes must be live before its chord is replaced. The four shared-widget action categories admit multiple independent live instances, each guarded by the same actual type/resource checks; weapon X and Board ZL also check their selected-record hashes. Unrelated callers and selectors cannot attach through those hooks. Generated guest C is never edited by hand.

## Actions, bindings and display policy

The Appendage chord is virtual R held with Y or A for previous/next appendage. The inspected input dispatcher connects the R shift state with Func66/Y and Func67/A on the controlled actor. Artwork lookup is separate from action consumption and physical binding translation.

`input_glyphs.c` translates `B_R`, `B_Y`, `B_A` through a copied current InputMap at each eligible draw bind. Each action needs exactly one supported binding with no other action sharing that physical input. Multiple, shared, unbound or unsupported mappings retain original virtual Switch labels for the entire chord and explain the reason in Controls. Original fallback is a truthful virtual label, not a claim that the host button has that lettering.

Manual styles change artwork only. They do not change physical mappings, emitted input or the existing rebinding editor. Standard face/shoulder/trigger buttons are supported for PlayStation, Xbox and Steam Controller artwork. Keyboard/Mouse supports current unshifted letters, digits, Space and left/right mouse; unsupported labels fall back. Steam styling covers exposed standard controls, not arbitrary paddles or touch regions.

Automatic uses copied physical activity before keyboard/pad merge and scripted input. It observes fresh keys/buttons, sustained strong stick/trigger intent and sustained captured camera motion. Held inputs, small drift, reconnect/focus baselines, incidental motion and single spikes do not repeatedly steal selection. These thresholds do not change gameplay dead zones. SDL touch/pen-generated mouse events are excluded from intent without changing gameplay handling. Arbitrary unmarked OS injection cannot be reliably identified.

Family detection uses SDL's exposed gamepad type. Steam Input presenting an Xbox interface is treated as Xbox; hidden hardware is not inferred. Nintendo family selects Original/native artwork. Unknown family and no intentional source use original labels. Intent remains tracked in manual mode so returning to Automatic uses the last intentional source.

The weapon guide translates virtual X (Change Weapon, Func7); the field guide translates virtual A. These use the same current-binding policy individually, so an unsupported remap falls back for that guide without disabling an otherwise supported Appendage chord. Space is included for the default X keyboard binding. Controls reports binding fallback by guide.

The preference `glyph_style = original|auto|playstation|xbox|steam|keyboard` is persisted independently from `pad_layout` and `bind_*`. No Lua loader/provider or external API is required.

## Draw selection, refresh and ownership

`appendage_glyph.c` scopes only an eligible live node's original draw/bind call, the same Cpu and the verified imported BindTexture return. It tags only the copied immediate texture handle. The original texture/sampler low 32 bits remain intact; the upper tag carries immutable generation and artwork IDs. No guest resource, command memory, active-bank field or global atlas is modified.

The selected record resolves through resource texture table[0], GetTextureHandle and BindTexture to the host texture. Source registration serial/storage identity is captured under the texture lock and checked again at replay. Those scalar equality guards never dereference retired storage. A reused texture ID cannot silently adopt a different registered source. Shader-type, feedback, freshness and descriptor resolution retain full-handle identity in their relevant lookups/memos.

Each artwork owns an immutable 512×192 RGBA image outside guest texture slots and alias caches. Its licensed 64×64 tile occupies the five verified UV regions; the remaining pixels are transparent. Original geometry, UVs, clipping, alpha, animation, material/sampler and draw order are preserved. There is no additional draw or covering overlay.

Pixels, image allocation and mapped staging upload are checked. Upload uses the existing upload batch and deferred staging retirement. Views use existing view caching. Private images and CPU pixels remain owned until renderer shutdown, independently of guest-node destruction. Shutdown waits for upload/GPU completion before retiring views/images and freeing pixels. Pixel/image/upload/view/source failures retain original rendering. Existing renderer-wide fatal infrastructure failures and device-loss recovery are not newly solved. At most 75 images are created on demand; each has one upload attempt per renderer lifetime.

Style/binding/source changes take effect when the guide records its next original eligible bind. Existing recorded commands retain their copied selection. No guest dirty setter is invoked from an arbitrary thread, no menu is repeatedly rebuilt, and no borrowed node/resource pointer is cached after retirement. Refresh of indefinitely cached commands outside this verified guide is not supported. Natural guide timeout and reappearance on retargeting remain vanilla behavior.

### Generation-history limit

There are **192 successful button-node attachment records per process**, shared by the three Appendage categories and all live instances of the four shared-widget action categories. With Appendage alone this allows at most 64 complete three-node constructions; the additional guides consume the same budget. This is not a frame, elapsed-time or enemy-target count. Repeated selection and A/R/ZL changes on a retained shared-widget node consume no new record; the immutable generation describes the source, while each copied command carries its selected artwork. Reconstruction and Appendage construction do. All registered live shared-widget instances are eligible independently; only Appendage retains its latest complete-trio requirement. Partial constructions can consume records without producing a complete visible guide. Attachments consume the same budget even while Original is selected, unless the adapter is disabled with `XCX_APPENDAGE_GLYPH=0`.

Previously attached complete guides can keep rendering after the budget fills. New attachments cannot be registered, so rebuilt guides retain original labels for the rest of that process. An incomplete Appendage trio retains originals for the whole chord. Controls reports **glyph history exhausted; restart game to restore styles**. Changing the style, reconnecting or retargeting does not reset the history. Copied commands remain valid through their existing resource checks; records are never recycled while commands may reference them.

This remains a visible experimental limitation for review; unlimited-session style support is not claimed. Removing it would need a separately reviewed command-lifetime/retirement design. It is intentionally unchanged in this feature.

## Licensed assets and reproduction

Two packs supply the 139 style tiles, each under its own license:

- **Audune Prompts 1.3.1** by Audune, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), <https://audune.itch.io/audune-prompts>: the 58 Xbox and PlayStation tiles (`assets/appendage-glyph/audune/`). Each is one 64x64 cell cut, unscaled, from the pack's "Light" sprite sheet by [tools/make_audune_tiles.py](../tools/make_audune_tiles.py); the D-pad direction tiles have their white and dim arms swapped (Audune draws the pressed direction dim; here it is the bright one), and the four PlayStation *colored* face tiles also have the dark symbol repainted in the symbol color of the pack's "Dark" sheet. The notice with the attribution line and the change statement is [audune/NOTICE.txt](../assets/appendage-glyph/audune/NOTICE.txt); the package, the two sheets and every tile (source cell, change, SHA-256) are in the `audune` block and the `pack: audune` entries of the provenance. The tiles are CC BY-SA 4.0, not MIT. Whether the compiled copy in `runtime/glyph_art.inc` and a binary that embeds it must also be offered under share-alike terms is a project decision that has not been made (see the controls context); the include's header states the license.
- **Kenney Input Prompts 1.5A**, CC0: the 42 keyboard tiles (letters, digits, mouse, Space, Tab, Ctrl and Shift), the 10 keyboard-cluster composites (nine key groups and the arrow cluster with only left and right lit, drawn for the page-turn circles) (`tools/make_keyboard_clusters.py`) and the 29 Steam tiles (the Audune pack has no Steam set). The original [license](../assets/appendage-glyph/License.txt) and [third-party notice](../THIRD_PARTY_NOTICES.md) accompany them; exact archive member names and per-file SHA-256 values are in [styles/provenance.json](../assets/appendage-glyph/styles/provenance.json). A file-specific Git attribute preserves the upstream license bytes. The older R1 PNG/include/tool remain an attributed reproducibility fixture, not the production multi-style asset path.

No Nintendo resource pixels are distributed. Xbox and PlayStation tiles follow the Audune layout: standard Xbox faces are the pack's Switch-set letters A/B/X/Y, colored Xbox faces its Xbox tiles, PlayStation Start/Back are the pack's DualShock 4 Options (three lines) and Share (rays) buttons.

| Artifact | SHA-256 |
| --- | --- |
| Original Kenney package | `ac2fcf599080b0f3ba2d174c9474db6df1a0e96ff0662580e2da79a122ab78a1` |
| Original Audune package | `9b94d0cf744b73f2d43ca328d8ebf6ad14f382ec4d04f1a03543a074db4cf55c` |
| Kenney license | `ca2f58f80f903512119da5828868199478119818cd7ecffb303d625cb74cbb5a` |
| Audune notice | `8d91c57e0c50cfcecaee561b3b99f1acbb90ea2148ac353a1b06cd0a6b567e9f` |
| Styles provenance | `6c12efae5a1909f23a55759a84acd0201a96b9f8e8139f2b582b161b2aace79b` |
| Production licensed derivative | `3f953f7cde1de407e4ed47cb058291ce781189c502cd59824e22c6b4ebf0a098` |

Reproduction needs Python 3.14 with `compression.zstd` and Pillow (verified with Pillow 12.3.0). From the repository root:

```sh
# Only to rebuild the Audune tiles from the downloaded package (idempotent):
# python3 tools/make_audune_tiles.py --zip /path/to/audune-prompts-1.3.1.zip
python3 tools/make_glyph_styles.py --out build/glyph-repro/styles.inc
cmp build/glyph-repro/styles.inc runtime/glyph_art.inc
python3 tools/make_appendage_glyph.py --out build/glyph-repro/r1.inc
cmp build/glyph-repro/r1.inc runtime/appendage_r1.inc
```

The production include contains 139 individually compressed licensed tiles, totaling 74,836 compressed bytes. The tool verifies source hashes and dimensions before writing the derivative. Reproduction does not read game assets or require a GPU.

## Build and checks

Use the toolchains and dependencies in [Building](building.md). This adapter is for `upd` / verified XCX 2.0.0; do not substitute the older versions named in general build examples. Runtime `.c` files are discovered by the existing build tool. A runtime-only change requires a native rebuild, not regeneration. Generator/site changes require regeneration with the pinned ExeFS and preserved patch file, then native rebuild and relevant differential checks. Asset changes require licensed-derivative regeneration and asset checks. No extraction is needed when you already have the verified inputs.

Example from the repository root, using your own verified input paths:

```sh
python3 recomp/gen.py --exefs /absolute/verified/exefs --out generated/upd --patches recomp/patches-upd.txt --jobs 2
python3 tools/build_game.py --version upd --gen generated/upd --out build/glyph-styles --exefs /absolute/verified/exefs --romfs /absolute/verified/romfs --expected tools/data/expected-xcx-2.0.0.json --profile /absolute/disposable/profile --jobs 2
```

Do not overwrite `patches-upd.txt` or edit emitted guest C. Existing generated/native outputs and all game-derived files remain ignored. Windows source support is present, but this feature's native Windows build/runtime is unverified.

Linux fixture prerequisites: Clang with ASan/UBSan, libzstd, Python 3.14 and Pillow. Use a new output directory to preserve earlier evidence:

```sh
python3 tests/appendage_glyph_test.py --out build/glyph-tests-review
python3 tools/check_content.py
python3 tools/check_content.py --staged
git diff --check
```

The default fixture run performs no game/GPU launch. It covers policy/action/current bindings, intentional input, licensed assets, hook guards and enabled/disabled retirement/address-reuse/exhaustion. Its mutex/input/hash services are explicit test seams; it is not a scheduler race test. For generator changes, also run the relevant [Unicorn differential checks](building.md#checks); hook fixtures do not replace them.

After coordinating GPU use, `python3 tests/appendage_glyph_test.py --out build/glyph-tests-gpu --gpu-build build/glyph-styles` links the isolated build's objects and tests real upload/readback, SDL filtering, source/view fallback, slot reuse and shutdown. Check that the native objects are current first. These are Linux POSIX fixtures; Windows acceptance is separate. The native `build/glyph-styles/xcx --selftest` needs no game files or GPU/window.

Diagnostic-only controls: `XCX_APPENDAGE_GLYPH=0` disables the adapter; `XCX_APPENDAGE_GLYPH_CONTROL=/absolute/file` provides the bounded comparison gate; `XCX_APPENDAGE_GLYPH_FAIL=pixels|image|upload|view|source` exercises fallback. They are not additional user preferences. Transition logs are bounded and can exhaust before a later style change; absence of a later log is not evidence of missing rendering.

## Appearance and native Nintendo mode

`glyph_appearance = standard|colored` changes only applicable licensed artwork after the copied binding decision. It has separate persistence and does not change device family, source intent, pad layout or emitted inputs. The pinned package supplies four colored face buttons each for PlayStation, Xbox and Steam. Buttons with no colored member retain Standard; keyboard/mouse and native Nintendo are unchanged.

Original leaves the game's original handle unchanged. Automatic chooses Original/native artwork for an exposed Nintendo family after intentional pad input. Native artwork also keeps the original virtual labels for custom bindings; it does not claim translated physical labels. The redundant explicit Nintendo Switch choice was removed at the owner's request after acceptance. Thus it needs no Nintendo pixel extraction, registration or upload.

The 75 licensed art IDs require seven bits at handle bits 32–38. Reserved bits 39–43, scalar generation bits 44–55, upper tag and original low 32 bits retain their roles. Fixed storage is 128 artwork slots, 75 used, with lazy allocation and existing shutdown. This expands the finite art index only; the 192 generation history and ownership model are unchanged.

See [coverage](prompt-coverage.md) for the resource-group distinction, Target/Board source evidence and current acceptance boundary. Runtime/artwork changes in this pass require a native rebuild and licensed-include regeneration, not new guest-code regeneration; no new generator sites were added beyond the preserved shared-widget work.
