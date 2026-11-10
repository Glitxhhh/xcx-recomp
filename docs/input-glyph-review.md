# HUD glyph styles reviewer guide

Review this as an experimental adapter for the Appendage and shared-widget guides listed in [coverage](prompt-coverage.md). Appendage, weapon and NPC interaction are owner-accepted; other field-A captions, Target R and Board ZL remain visually unconfirmed. The feature defaults to Original. It does not replace HUD paths outside that inventory or menu/tutorial/QTE prompts, change input mappings or add Lua integration. Player instructions are in [Controls](controls.md#experimental-appendage-glyphs); build identity, commands and contracts are in the [developer reference](input-glyph-styles.md).

## Suggested review order

1. **Assets and attribution:** inspect `assets/appendage-glyph/styles/provenance.json`, the original CC0 license, `THIRD_PARTY_NOTICES.md`, and both reproducible asset tools. There are 75 production tiles plus the preserved historical single-R1 fixture. PNGs are exact pinned Kenney archive members; includes are licensed-only derivatives. No game-derived atlas pixels, guest code or binaries belong in this change. *(The local trial branch has since replaced the Xbox and PlayStation tiles with Audune Prompts, CC BY-SA 4.0, and added keyboard clusters: 137 tiles from two packs, see [Licensed assets and reproduction](input-glyph-styles.md#licensed-assets-and-reproduction). The counts and hashes below describe the accepted Kenney-only feature.)*
2. **Build/site validation:** read `recomp/appendage.py`, then its small `gen.py` / `nso.py` changes. Verify all thirteen instruction words, exact main identity and original call/spill preservation. Do not infer action meanings from artwork selectors.
3. **Mapping and intent:** read `runtime/input_glyphs.c`, then the display-only snapshots and adapters in `input.c`, `input_sdl.c`, `input_win.c` and `input_wsys.h`. Check whole-chord Appendage and per-button shared-widget unsupported fallback, manual/Automatic separation, exposed-device family policy and original input behavior. SDL and Windows have different source-observation adapters; Windows has not been accepted.
4. **Draw and retirement:** follow `runtime/appendage_glyph.c` through `nvn_api_cmd.c` to the four full-handle resolution points in `vk_exec_draw.c` and the private immutable images in `vk_texture.c`. Check guarded live use, retirement before teardown/reinitialization, nonrecycled scalar generations, source registration identity, upload/fence/view ownership and shutdown. There is no global atlas mutation or guest dirty-setter call.
5. **Controls and persistence:** inspect `cfg.c`, `ui/menu.cpp`, `ui_text.c` / `.h` and the user docs. Original is default, the preference is independent of pad layout/bindings, unsupported builds disable it, and fallback reasons remain visible.
6. **Fixtures and limits:** inspect the five test files and the generation boundary below. Synthetic hash/input/mutex seams do not prove guest resource ownership or scheduler races; the separate native GPU evidence covers the actual upload/view paths.

The implementation extends the preserved one-glyph prototype from baseline `b31f16572baf4518927eada59e65916b072e1359`, on reviewed master `7b2337a6854b3936dc4f567dbb46add8c1eb6559`. Local import/provenance and recovery manifests preserve the original 20-file source snapshot. No naming/controller-keyboard, Lua/Mods, Online or HUD branch code was combined. That history predates upstream merge `3896b5b`, which includes reviewed master `59803d9`. The merged `recomp/patches-upd.txt` is preserved with SHA-256 `7e9db23915ce32b390b3a4797cc15a6eceba52f7ec0185472c27a2a18c25e8c1`. The new uncommitted extension does not change those patches.

## Generation-history assessment

The process-wide maximum is 192 successful node attachments, shared by the three Appendage slots and four shared-widget action categories. With Appendage alone that is at most 64 complete constructions; other attachments consume the same budget. Previously attached complete guides remain eligible after it fills. Later reconstructed nodes cannot attach, so their new binds retain original labels until restart. Partial construction can consume budget sooner; selecting Original does not stop attachments. Repeated shared-widget selection and drawing frames consume nothing. No fixed number of enemy targets or play duration is guaranteed.

This bounds copied-command metadata and avoids unsafe record reuse while native commands may outlive a guest node. It also limits how long styles remain available in sessions that reconstruct the guide repeatedly. Controls now distinguishes terminal exhaustion from waiting for nodes and tells the player to restart. Style changes and device reconnect do not reset it. The scope remains experimental; reviewers must explicitly accept this user-visible limit. Removing it needs a separate command-lifetime design, not a larger silent cap.

The boundary fixture proves the last full trio still selects artwork at 192, the next construction uses original handles, generation count does not grow, and the status names exhaustion/restart. Earlier retirement tests protect the destroyed node's page and reject later access; copied scalar command identities remain checked independently.

## Validation evidence

Earlier Linux owner acceptance covers the Appendage modes, explicitly including **Automatic and Keyboard/Mouse**. The supplied screenshots show PlayStation, Xbox and Steam Controller styling on a PlayStation controller. This is artwork/mapping acceptance, not physical Xbox/Steam hardware acceptance. The owner confirmed normal guide timeout as vanilla behavior. No repeat Appendage-only acceptance is needed. The weapon shared-widget path now has owner acceptance; field A has live attachment evidence but no confirmed visible caption/artwork.

| Check | Evidence and limit |
| --- | --- |
| Final focused fixtures | 4,108 policy/action/current-binding/intent/tile/status checks; ASan+UBSan enabled and disabled node fixtures passed |
| Generation/status regression | Last complete trio at cap, next construction fallback, terminal restart reason, and one copied runtime-status observation passed |
| Asset reproduction | Both includes regenerated to new ignored fixture outputs and matched byte-for-byte; all 63 curated PNGs and original license matched the pinned archive |
| Native review build | Linux incremental compile/link passed at `-j 2`; headless `--selftest` passed with zero failures; no window/GPU/game run |
| Existing full generation | 197 chunks emitted; zero unsupported instructions; only glyph chunks 34/59 and baseline cloud-patch chunk 58 differed from older output |
| Existing Unicorn differential check | 36 functions / 561 executable cases passed; 943 skipped; this is not direct execution of the six game sites |
| Existing SDL adapter fixture | Touch/pen-generated events excluded from intent while gameplay states/deltas stayed intact; genuine input and focus clearing passed |
| Existing GPU fixtures | All 62 private atlases uploaded/read back; normal and forced pixel/image/upload/view/source fallback, copied views, slot reuse, retirement and shutdown passed on RADV |
| Content and Git checks | Source/staged content scan, whitespace, pinned patch hash, preservation manifests and full read-only Git object checks passed during preparation |

The historical review results below precede the new extension. At that review, fixes affected only status reporting: terminal budget exhaustion was previously described as waiting, and `glyph_status` previously read a changing runtime note twice. It now copies that result once. The test driver also accepts a separate output directory so review checks preserve older logs. Renderer selection, ownership, input behavior, resource checks, generation limit and assets are unchanged. A file-specific Git attribute preserves the original license bytes and its upstream whitespace; staged/committed bytes match the pinned archive rather than a line-ending-normalized copy.

| Candidate | SHA-256 |
| --- | --- |
| Owner-tested Linux executable | `558a95e11c3aaf75666281af609973c16c6d0ed349d8163345403f4275249265` |
| Native build with the two status fixes | `d9e04c6cf7b56dc01a4d58596f5f4b5b0a0bd550af5c717f65439476c5bb961b` |

The rebuilt binary passed focused fixtures/native selftest, but was not shown in another owner game session. The rendering/input paths accepted by the owner are unchanged; no specific owner retest is necessary for these status-only corrections. Native artifacts and logs stay ignored.

## Remaining limits

- Native Windows build/runtime and Vulkan validation-layer coverage are unverified. `VK_LAYER_KHRONOS_validation` was unavailable in the existing GPU runs; zero reported errors is not layer coverage.
- Actual Xbox/Steam hardware, arbitrary hidden Steam Input mappings, unsupported keyboard labels/bindings, general cached-command refresh, device-loss recovery and unlimited session generation history are not claimed.
- The prior global 48-message diagnostic cap is replaced by a first-binding observation per generation plus at most eight transitions per action category (conservative maximum 248 binding lines), so one active group cannot consume every group’s evidence. Missing later lines still do not establish a rendering defect. Upload/intent logs and owner observations remain separate evidence.
- Owner acceptance is explicit for modes, Automatic and Keyboard/Mouse. Reconnect/focus/resize were part of the requested sequence, but no independent per-step measurements were recorded. No new telemetry is invented from the overall success reply.
- Colored face buttons are now implemented for the four PlayStation, Xbox and Steam face buttons. Missing variants retain Standard; Nintendo native and keyboard/mouse are unchanged. Other appearance variants remain outside this pass.

## Review disposition

The accepted Appendage baseline was ready for local PR review. The current shared-widget extension has passed its weapon-guide owner check. NPC interaction field A is owner-accepted, including Button layout changes; other field-A captions, Target R and Board ZL remain visibly unconfirmed. Their guards and fixtures have passed. The explicit generation limit, Windows gap and unavailable validation layers remain review decisions; this guide does not claim merge approval. Keep local machine recovery history, profiles, captures, extracted assets, generated guest C and binaries out of commits. The owner authorized this reviewed baseline commit and normal branch push; PR ready/merge actions remain excluded.

## Shared-widget extension checks (2026-11-08)

- Final pure policy and licensed-pixel fixture: 8,209 checks passed. ASan/UBSan enabled, disabled and new `guides` fixtures passed. New cases cover exact parent/inner caller filtering, different Cpu/unrelated selector rejection, wrong type, retained-node reuse, retired protected memory and address reuse.
- Both licensed derivatives reproduce; the production set now contains 63 tiles, adding the pinned Space key. No game pixels were introduced.
- Full generation: 197 chunks, zero unsupported instructions. The preserved merged patch table was used.
- Whole-function Unicorn comparison: 285 executed functions / 1,840 cases, zero failures; 2,864 cases skipped. This randomized test does not itself execute the new runtime adapters; their caller/state behavior is covered by the dedicated fixture.
- Final Linux build at two jobs and headless selftest passed; all 304 native object hashes verified without mismatch.
- New candidate SHA-256: `23f67263491ed32e4317b6a11febb628157023c9765cfe715bfbbfd80e9055a0`. The 558a95 owner-tested candidate remains preserved.
- Live weapon node/resource/selected-record guards passed; the owner confirmed styles, Space, Automatic and untarget/retarget. The same candidate then passed an owner-requested original/PlayStation Appendage clarification. Field A nodes attached twice but their visible caption/artwork remains unconfirmed. Earlier 62-art GPU readback evidence is historical; the new run establishes live fourth-region appearance/upload, not a repeated automated readback suite. No new Windows or validation-layer run is claimed.

Evidence and the prearmed, 180-second disposable-profile launcher are ignored under `build/prompt-next-group/`. No new work is committed or published.

## Native Nintendo, appearance and field-widget extension

The accepted candidate added Automatic Nintendo/native-art selection and independent `GLYPH_APPEARANCE` persistence. Its redundant explicit Nintendo Switch style choice was subsequently removed at the owner’s request; Original remains first. Original remains unchanged game rendering. Native mode deliberately retains virtual labels for custom bindings; it does not claim translated physical labels. Colored face buttons add twelve exact CC0 members, with Standard fallback for other buttons/families. The saved Button layout and bindings are unchanged.

The shared field widget now admits Target R and Board ZL through the already scoped call. Board verifies its record hash and fifth UV region. Retained field nodes transfer their source-only scalar generation across A/R/ZL selectors without growing history; retirement still prevents address-reuse adoption. The immutable artwork field is explicitly seven bits, with 75 licensed images within fixed capacity 128. Source serial/storage and GPU ownership rules are unchanged.

- 14,620 pure policy/pixel/intent checks passed, including Nintendo detection policy, native/original handles, 12 appearance variants, Standard fallback and the fifth UV region.
- ASan/UBSan fixtures pass for filtered callers, colored Automatic handles above art ID 63, Nintendo native fallback, retained A/R/ZL selector changes without history growth, independent slots, retirement/reconstruction, source checks and exhaustion.
- All 75 asset files match the pinned Kenney archive byte-for-byte; the licensed include reproduces exactly (24,184 compressed bytes).
- Native Linux two-job build and headless selftest pass; all 304 object hashes verified. No new generator sites or guest-code edits were required for this runtime extension.
- After owner coordination, SDL filtering and all six GPU stages passed on RADV: all 75 images uploaded/read back, new style/appearance settings persisted independently from bindings, and normal/pixel/image/upload/view/source fallbacks, copied commands, retirement and shutdown passed. The first container invocation lacked desktop environment variables and stopped at initialization; the corrected run passed. This was a fixture setup failure, not a guest-game crash. Validation enabled 0 because the layer is unavailable. The owner confirmed the consolidated appearance check worked, then requested removal of the redundant Nintendo option. No Windows result is inferred. Field A/Target/Board visible caption correspondence is not established by that overall reply.

Candidate SHA-256: `8eb0575cb058f3168075d82847dfcd3a9cff2a604221d5347b753a58eed70161`. A separate hash-verified copy of the previous 19 uncommitted files is retained under ignored `build/glyph-appearance-20261108/preserved-source/`. All old candidates and reports are preserved.

After the owner-requested option removal, focused ASan/UBSan fixtures and native selftest passed again. Corrected native executable SHA-256: `13d5a46b340aa9fd0d303b73e9b1039da337c717aaa738ca44163927ffc8e9e4`. Renderer, assets, physical mappings and colored-appearance behavior are unchanged; no repeat game/GPU test was performed or requested.

## Shared lookup consolidation and concurrent field instances

The current pass removes two outer wrapper hooks and passes the selector’s live frame explicitly into the existing inner lookup hook. Verified saved-parent LR and selector filters retain the same admitted components. All registered shared-widget instances remain eligible until retirement instead of keeping only the latest instance per action. This broadens the existing field-group coverage without adding per-widget hooks or assuming every atlas consumer has the same semantics. Appendage’s complete-trio behavior and the 192 scalar-generation cap remain unchanged.

The unimplemented `0x5ae880` candidate was ruled out for Issue Orders: source ID `0x13c8` resolves to Modify Avatar. Its actual menu action/material path remains unverified and no adapter was added. The resource inventory names the remaining directional, white-mask, menu/tutorial and complete-atlas fallback contracts. See [coverage](prompt-coverage.md).

Current focused checks: 14,620 policy/pixel checks; ASan/UBSan enabled/disabled and saved-frame/concurrent-instance fixtures pass. Full generation emitted 197 chunks with zero unsupported instructions; the generated helper receives the live local FP explicitly (`x29`). Whole-function differential comparison passed 1,840 cases on 285 functions, with 2,864 skipped; it is not direct runtime execution of every hook. Two-job native build and headless selftest passed; all 304 object hashes verified. All 75 asset members and the original license match the pinned archive, and the licensed derivative reproduces exactly. Content and whitespace checks passed.

After owner coordination, the separate GPU/SDL regression fixtures passed all 75 image readbacks, filtering/persistence and six fallback/retirement stages. Validation layers were unavailable. Candidate SHA-256 `7451906837ba97ec8a05004afa884b0026782cfb40ff2e155e481cac1c7eab95` is prepared with a fresh copied disposable profile. Its launcher/stop commands and pending field-guide/custom-binding checklist are in ignored `build/glyph-shared-resources-20261108/acceptance.md`. The owner-coordinated game run closed normally after 169.62 seconds (exit 0). Screenshots confirm colored Appendage and weapon Square, while control diagrams, settings/tutorial guides, Soul Challenge B and + hints remain native. Target R has selected-draw evidence but no visible-caption acceptance; field A and Board remain unconfirmed. See the coverage inventory for the separate agent and owner observations.

Diagnostic output remains bounded: one first-binding observation per generation plus at most eight category transitions per action category, at most 248 binding lines for the unchanged 192-generation cap. A new field instance can therefore produce evidence even after another instance consumed that category’s transition budget. Active-selector reads occur under the live-identity lock and are omitted after retirement; no retired node is read for logging.


## Actionable-resource continuation — November 9, 2026

No additional prompts are styled in this pass. [The focused resource report](prompt-actionable-resources.md) identifies the shared inline text-icon family and separates Soul Challenge's circular background from its unresolved B label. A read-only opt-in resource/descriptor probe was added to the existing type-1 bind route; it adds no generator sites, replacement handles, texture ownership or input changes. It cannot observe text paths that bypass that route, and no lifetime/refresh result is inferred from preparation.

Four eight-event resource filters, expiry, unchanged Cpu/handles and cleared scopes pass ASan/UBSan fixtures; prior live-node retirement/concurrency checks and 14,620 policy/pixel checks pass. All 75 pinned licensed members and the license verify; derivatives reproduce. Linux two-job build verifies 304 objects; headless selftest passes. Content and whitespace checks pass. Candidate `2858fd455c90393ed8c080b820cec7aec7714a4f571bc766807e44ceb68d3797` is frozen with a new 46-file hash-verified disposable profile and prearmed one-shot launcher. The later coordinated game run closed normally (exit 0, 186.08 seconds, 39,699 bytes), with zero matching resource-probe samples. No new GPU or Windows run occurred. NPC interaction is owner-accepted; accepted style appearance need not be repeated.


## Owner NPC/menu check — November 9, 2026

The owner reports that approaching NPCs displays the interaction prompt correctly and changing Button layout updates it appropriately. This accepts the NPC interaction instance of field A; it does not accept every field-A caption, Target R or Board ZL. Agent logs independently show slot 4, active selector 7, drawing original and PlayStation Cross/Xbox A/Steam A variants. The supplied Gameplay Tips screenshot still shows native X, L/R and inline A while the host Controls overlay shows PlayStation/Colored selected. This is expected unresolved menu/tutorial coverage, not a failure of accepted HUD coverage.

The completed recording contains the ready line and zero matching resource-probe samples. Normal window close and exit 0 are verified: 186.08 seconds, 39,699 log bytes. The exact-candidate process scan is empty. Hashes and results are preserved in ignored `build/glyph-actionable-20261109/completed-run-manifest.json`. A lack of samples on the scoped type-1 path does not identify the text draw route or prove resource absence. Do not repeat this diagnostic merely to reconfirm native menu icons.


## Reviewed baseline checkpoint — November 9, 2026

The owner authorized a normal feature-branch commit/push and a separate inline-prompt trial checkout. The accumulated shared-widget guards/concurrent-node handling, colored artwork and Space translation, independent preferences, bounded opt-in resource observer, tests and documentation were reviewed together. No runtime or asset changes were needed during finalization; only acceptance/exit documentation and the local trial prerequisites were updated. Runtime, generator, tests, tools and assets match the source manifest for candidate `2858fd455c90393ed8c080b820cec7aec7714a4f571bc766807e44ceb68d3797`.

Fresh focused sanitizer/policy/reproduction checks pass, including all 14,620 policy/pixel checks and the bounded resource fixture. All 75 PNGs and the original license match the pinned archive. The same candidate passes headless selftest. The final recording shows normal close, exit 0, 186.08 seconds and zero matching resource samples. No new game/GPU/Windows run or native rebuild was needed. Ignored finalization evidence is under `build/glyph-baseline-20261109/`; the completed run manifest remains under `build/glyph-actionable-20261109/`.

[Trial prerequisites](glyph-inline-trial.md) document a fresh source checkout and regeneration/build steps. The exact pushed baseline and trial branch/worktree are maintained in the shared controls context. The remaining investigation is the inline text-icon draw/material/refresh route, not a repeat of this empty type-1 capture. The experimental generation-history limit, native Windows gap and unavailable validation layer remain explicit review limits; this checkpoint does not mark PR #118 ready or approve a merge.
