# HUD glyph coverage and resource groups

Appendage passed the Linux owner check, including Automatic and Keyboard/Mouse. The shared-widget extension adds tightly filtered HUD button guides. Weapon X passed the owner appearance and Automatic check. NPC interaction field A also passed, including changes with Button layout; other field-A captions, Target R and Board ZL remain visually unconfirmed. Normal hiding and reappearance on retargeting remain game behavior.

## Coverage inventory

This inventory records identified paths, not a claim that every prompt in the game has been enumerated. A bounded node-name scan found 214 names and 502 references across 195 functions; names alone do not establish a screen, action or draw route.

| Visible component | Source evidence in main | Coverage / remaining work |
| --- | --- | --- |
| Appendage R + Y / A | Constructor `0x60f324`; exact rectangle calls `0x60f68c/6a8/6c4`; type 1 and `icon_button.wilay` observed live | Accepted Linux coverage. Existing R shift / Func66 and Func67 actor dispatch findings remain unchanged. |
| Weapon-change X beside the weapon icon | `0x5a63cc` resolves `mc_Autoattack/mc_Weapon/mc_Btn`; `0x5a6614` selects 5 at `0x5a665c` through `0x5da974` | New caller-filtered coverage, now Linux owner-tested. X artwork is record 5. Field/battle input configuration independently identifies X / Func7 as Change Weapon. Live node/resource checks and selected draw binding passed; the owner confirmed style/Space appearance and normal targeting behavior. |
| Field A-button guide | Common constructor `0x5c6dd8`, exact call `0x5c6ed4` to `0x5da974`; dispatch `0x5c6ca4` chooses selector 7 for Help Up (`hud_FieldButton101`), Warp (`104`), Embark on Mission (`105`) | New adapter restricted to A; two live attachments passed the node/resource guards. R/ZL are admitted separately by the same scope; other selectors remain original. NPC interaction appearance and Button layout changes are owner-accepted. Other field-A captions remain visually unconfirmed. Per-caption gameplay consumption has not been newly observed; no input or action behavior is changed. |
| Field Target R | `0x5c6ca4 → 0x5c6d68`: selector 14, `hud_Target01` / Target; same `0x5c6ed4` widget call | New guarded adapter. Current virtual R binding; live appearance pending. No new generic-selector hook. |
| Field Board ZL | Same dispatcher case `0x5c6cd8`: selector 15, `hud_rideon01` / Board; same widget | New guarded adapter, additional selected-record hash and fifth UV region. Current virtual ZL binding; default keyboard has two ZL keys and retains original. Live appearance pending; no Skell/progression test required. |
| FRONT / SIDE / BACK positional caption | `0x59a864`, update `0x59b010`; localized IDs `0x63e/640/641`; `mc_GroupPallet/mc_AdvantageSpot` | Caption identity confirmed. It is separate from neighboring weapon X; not assigned an X action from screenshot proximity. No caption replacement. |
| ZL/ZR HUD guide | `0x5a90d0`, `mc_IconZLButton/ZRButton`, calls `0x5a9320/334` | Candidate only. Screen/action/node and binding semantics unresolved. |
| Modify Avatar header/tab L/R guides | `0x5ae880`, source message ID `0x13c8` resolves to `hud_FieldButton108` / Modify Avatar; `mc_FieldHeader/rect_Text`, L/R icon nodes | Unimplemented. Earlier Issue Orders attribution was incorrect. Actual tab/input consumer and live node/material remain unresolved. |
| Issue Orders / battle commands | `menu_program_BattleCommand01` / Issue Orders is message row 4929 (`0x1341`), not source ID `0x13c8` | Caption identified, construction caller not established. Do not reuse Modify Avatar’s caller. |
| Overdrive / Quick Cooldown guidance | `0x64171c` / `0x641d30`; identified message references | Distinct components; not Appendage or weapon guide. No verified button-node adaptation. |
| Map/navigation guides | `0x6889d4` and related map layout references | Candidate only; actual guide/action/draw path unresolved. |
| Menu button guides | Many `0x55fec0` callers | Generic selector is not a screen or action API. No global adaptation. |
| Tutorials, inline text and controller diagrams | No verified common route established | Unimplemented; may use text or image paths distinct from rectangle buttons. |
| Battle QTE / unrelated icon candidates | Earlier transient and generic traces did not identify a usable scoped route | Unimplemented; not reused for this extension. |

## Shared-widget source and guards

Both selected callers enter `0x5da974`, which supplies `mc_Btn/rect_Btn` to `0x55fec0`. That lookup resolves the rectangle through `0x915d00`, constructs a temporary wrapper through `0xa1cfc0`, and calls the selector setter at `0x55ff1c`. This wrapper stores the resolved node at offset **zero**, unlike Appendage's wrapper at offset `0x38`.

The single inner lookup hook requires exact return address `0x55ff20` and reads the saved caller at the verified selector frame +8. The source prologue at `0x55fec0` saves FP/LR and `0x55fecc` sets FP=SP; the tail branch from `0x5da974` preserves the original parent LR. The frame is passed explicitly to the helper so generated register planning publishes it correctly. Only the known weapon/field parent return addresses and selectors are admitted. The two outer wrapper hooks are removed. All unrelated callers, nodes and selectors remain untouched. Generation checks validate thirteen instruction words, including the saved-frame prologue and the pinned source identity.

A selected node must pass the existing type-1/vtable/resource-prefix/24-record/table checks. Weapon X additionally verifies its 28-byte record SHA-256 `e2c35ae28e5545ecfb6658b2503f3e062fd4216c4326090eaf496eee3b5da46d`. The original selected-record read must agree before tagging a bind. A different rectangle type or resource keeps original rendering and produces at most one rejection log per added slot. Such a rejection is a blocker for that guide, not permission to guess another layout.

The verified X record samples the 64×64 region beginning at `(320,0)`. Owned licensed images now populate that fourth region as well as the accepted three. The original rectangle's geometry, UVs, visibility, clipping, material, sampler and ordering are unchanged. No renderer allocation/descriptor/release design was expanded. The same source serial/storage checks, immutable tagged handles, existing upload and GPU-idle shutdown remain in use.

## Action and binding distinction

The existing local `SYS_PcBtlKeyList` row 10 and `SYS_PcFldKeyList` row 11 identify `BTN_X_FUNC_ChangeWpn`, Shift 1, Type1 2, Button1 7, Func 7. The inspected actor dispatcher entry `0x1c33c98` resolves to `0x204620`, which checks the controlled actor and calls `0x1f0650(actor,0x15,0)`. This evidence is independent of record-5 artwork. A new live X-consumption test is not claimed.

Weapon X and field A use the copied current binding map individually. The accepted Appendage chord still falls back as a complete chord. Multiple, shared, unsupported or unbound mappings keep original virtual labels for the affected guide. The pinned CC0 Space tile supports the default X keyboard binding. Controller family detection, pre-merge intentional-source policy and the saved style preference are reused without changing emitted inputs.

## Refresh and limits

Selection occurs when an eligible original draw bind records a command. A style, source or binding change applies at the next such bind; previously recorded commands keep their immutable selection. The extension does not force visibility, call dirty setters, rebuild menus or claim to refresh indefinitely cached commands.

There are seven action categories and the same 192 immutable attachment records. Shared-widget live identities are now retained per attachment, so constructing another instance no longer evicts a still-live guide. Repeated selection and A/R/ZL selector changes on a retained shared-widget node reuse its source-only generation. The copied command already carries its own immutable artwork ID; destruction/reinitialization retires the node before address reuse. Retirement clears its live eligibility; address reuse receives a fresh generation. Appendage still requires its latest complete trio. Every registered, still-live shared-widget instance is eligible independently; retirement clears its live address without changing copied scalar command identity. History exhaustion makes newly constructed guides original until restart, while already attached supported guides can continue. See [the generation limit](input-glyph-styles.md#generation-history-limit).

## Validation and acceptance boundary

The fixture covers the exact caller/Cpu/selector filter, unrelated lookups, wrong type, repeated selection, independent slots, retirement, protected retired memory and reconstructed address reuse. Existing tests cover disabled rendering, resource mismatches, immutable source identity and the 192-record boundary. Policy tests cover the new single-action decisions and Space pixels. Asset reproduction verifies 63 pinned Kenney CC0 tiles; no Nintendo pixels are tracked.

Ignored evidence is under `build/prompt-next-group/`: bounded source excerpts, instruction checks, local message/config observations, fixture logs, regeneration/build results and a hash-verified disposable-profile copy manifest. Old evidence and the owner-tested Appendage executable are preserved. The new appearance/resource evidence is separately ignored under `build/glyph-appearance-20261108/`. The new weapon check and owner-requested Appendage clarification are recorded below. Native Windows and Vulkan validation-layer coverage remain pending.

The branch includes merge `3896b5b` of reviewed upstream `59803d9`. The merged `patches-upd.txt` SHA-256 is `7e9db23915ce32b390b3a4797cc15a6eceba52f7ec0185472c27a2a18c25e8c1`; no older patch table was restored. Generator changes require regeneration from the pinned ExeFS followed by a native build; runtime-only changes need only rebuilding. Build with at most two jobs.

## Coordinated live checks

Candidate SHA-256 `23f67263491ed32e4317b6a11febb628157023c9765cfe715bfbbfd80e9055a0` was launched with copied disposable profile `glyph-shared-guides-20261108-01`. The prearmed launcher had a 180-second / 32 MiB limit. The first run closed normally after 88.79 seconds, exit 0, with 35,882 log bytes. No debugger was attached.

Agent observations: field A slot 4 attached generations 1 and 2; Appendage slots attached generations 3–5; weapon X slot 3 attached generation 6. All used source texture 824. The weapon node passed selected-record checks and requested PlayStation Square; subsequent Xbox X, Steam X and Space images uploaded through the existing owned-image path. No guard rejection was logged. No field A draw/visible caption correspondence was established, so its attachments alone are not appearance acceptance. No new destruction/fence instrumentation was added; fixture/source ownership evidence remains separate.

Owner observations: confirmed selected weapon styles and Space, Automatic and untarget/retarget all behaved normally. The owner then requested one additional launch to clarify the separate Appendage guide. The same hash/profile was used, with separate preserved logs under `build/prompt-appendage-confirmation-20261108/`. The owner confirmed both original and PlayStation Appendage versions showed. No new naming, progression, QTE or lengthy combat test was requested.

The owner-requested Appendage clarification closed normally after 32.26 seconds, exit 0, with 28,245 log bytes. Both sessions stayed within their limits. Both exact-candidate process scans are empty; the build container was stopped. No commits or pushes were made.

## Centralized resources and safe sharing

The checked archive contains multiple button resources, not one universal atlas. These are local inspection findings; extracted pixels remain ignored and are not distributed.

| Resource | Contents established offline | Consumers / replacement status |
| --- | --- | --- |
| `/ui/image/icon_button.wilay` | 512×192 BC7; 24 records at +0x214. Records 5/6/7/8 are X/Y/A/B; 13/14/15/16 are L/R/ZL/ZR. Other records include d-pad, system and stick icons. | Live prefix/table/type/selected-record evidence for Appendage and scoped HUD widget. Licensed private images share this verified draw route across those groups. |
| `/ui/image/icon_button02.wilay` | 352×88 BC7; four records: L vertical/horizontal directions, R vertical/horizontal directions. | No positive live consumer/type/material/lifetime association established. Not adapted. |
| `/ui/image/icon_button03.wilay` | 368×150 BC7; 24 white-mask records with matching button categories, different dimensions/UVs. | Not interchangeable with the first atlas. Exact live consumer and tint/material path unresolved; not adapted. |
| `/ui/image/common_button.wilay` | LAGP layout/container with 8×8 backing image. Includes `icon_button`, `mc_Btn`, `rect_Btn`, `rect_BtnRed`, ring/mask names. | The common HUD widget constructor uses `mc_Btn/rect_Btn`; the actual resolved rectangle is separately guarded as type 1 and first-atlas resource. The container itself is not the 24-record texture atlas. |

A shared resource identifies artwork, not an input action or chord. Swapping an entire atlas would apply one physical-binding assumption to unknown consumers and could affect masks, stick diagrams, cached recordings or different material paths. The safe sharing supported here is one owned-image policy and bind/retirement implementation behind one saved-caller-filtered shared lookup hook. The existing shared-widget call handles A, R and ZL without adding per-button hooks. Menus, tutorials and the other resource groups remain outside verified coverage.

The common field dispatcher also selects A for Field Action, Check, Talk, Open, Install Data Probe, Access, Change Time, Modify Avatar, Call and Activate, in addition to Help Up, Warp and Embark on Mission. These captions are resource/source identities, not new owner acceptance. Live field A attachments were already observed, but a visible caption/button association still needs the consolidated check.

The independent input configuration identifies field R / Type4 Button1 9 / Func2 and field ZL / Type2 Button1 13 / Func20. Verified callback-table relocations resolve Func2 to `0x204248` and Func20 to `0x2052fc`, which follow the controlled-actor action route. The verified converter `0x2094ec` reads button IDs 9/13 as masks 0x20/0x40 from `0x17f1498`. Npad conversion `0x13ab080` tests virtual Npad bits 7/8 and writes those same masks at `0x13ab1c8/1d8`. `input_map.h` defines B_R/B_ZL as those Npad bit positions, and `ov_input.c` passes the copied polling result into Npad state. This connects the virtual inputs independently of the R/ZL artwork. Existing controlled-actor input production preserves those low bits; no broader edge/repeat semantics are claimed. Selector IDs are not used as host action IDs. The copied host InputMap translates B_R/B_ZL only at eligible draw recording.

## Nintendo, appearance and upstream distinction

Original retains unchanged game rendering, and Automatic selects the same native artwork for intentional activity from an exposed Nintendo controller. The redundant explicit Nintendo Switch choice was removed at the owner's request. Original/Automatic use the game's existing handle; neither extracts or distributes Nintendo pixels or relabels native artwork for custom bindings. Physical Nintendo hardware acceptance is separate from the source/policy test.

`glyph_appearance = standard|colored` is independent of `glyph_style`, `pad_layout` and `bind_*`. Twelve exact pinned Kenney members add colored face buttons for PlayStation, Xbox and Steam Controller, bringing the licensed set to 75. In the local trial the Xbox and PlayStation tiles (including the colored faces) are replaced by Audune Prompts 1.3.1 (CC BY-SA 4.0, see [input-glyph-styles.md](input-glyph-styles.md#licensed-assets-and-reproduction)); keyboard and Steam stay Kenney, and the set is 137 tiles. Missing variants keep Standard; native Nintendo and keyboard/mouse remain unchanged. Automatic applies the selected appearance after family/binding translation.

The immutable artwork ID now uses bits 32–38 (seven bits); reserved bits 39–43, generation bits 44–55 and the upper tag remain separate. Fixed capacity is 128, with 75 available images allocated only on demand. The 192-record generation history is unchanged. No new upload/release mechanism or global atlas mutation was introduced. The fifth licensed UV region is Board ZL at (448,64), with exact record hash `7ce0a3a3a806647b28e56f64b57346a6360500bdc5420525ca7e5de94fad4fa9`.

Button layout is existing upstream behavior: upstream `input_map.c` has Nintendo/Xbox physical face-button maps and `menu.cpp:tab_controls` exposes `XCX_PAD_LAYOUT`. That option changes which virtual actions physical buttons emit. Glyph style and appearance change only what verified guides display. This pass does not modify the physical map.

Upstream was checked again: latest master `feb125c` adds only Division Rewards clock handling in `runtime/ov_system.c` beyond the reviewed `59803d9` baseline. It is unrelated and was not merged or copied into this feature. The conflict-resolution merge and patch table remain intact. Naming documentation stays on its own branch; Windows still uses the native Win32 popup and has no reachable SDL controller grid. No Windows keyboard implementation or Lua changes are included.

## Appearance candidate acceptance and final option correction

After owner coordination, all 75 licensed images passed GPU upload/readback and all six fallback/retirement/shutdown stages passed, along with SDL filtering and independent style/appearance persistence. Validation layers were unavailable. A first container invocation lacked desktop environment variables and failed at fixture initialization; that evidence is preserved separately from the successful desktop-environment run. It was not a guest crash.

Candidate `8eb0575cb058f3168075d82847dfcd3a9cff2a604221d5347b753a58eed70161` closed normally after 100.79 seconds, exit 0, 36,928 log bytes. Agent logs show colored PlayStation/Xbox/Steam face-art requests/uploads, original handle retention, keyboard artwork and node attachments for all added field slots. No selected draw bind for slots 4/5/6 was recorded. Their visible caption/material correspondence remains unresolved; the smallest next probe is one immediately accessible field A or Target caption with its exact live node, not another general battle test. No Board progression test is requested.

The owner said the check worked and asked to remove the separate Nintendo Switch choice because Original already supplies the desired native artwork. That redundant option was removed; Automatic Nintendo still selects Original/native artwork. Original remains the first choice and appearance stays independent. No physical mapping or renderer/asset change accompanies this correction. Focused fixtures and a native rebuild/selftest are sufficient; no repeat gameplay or GPU acceptance is requested for the option removal. The tested executable and its 36-file source snapshot remain preserved separately from the corrected build.

## Shared-resource pass: current implementation and limits

The broadest justified expansion is **all live instances of the verified common HUD button widget**, rather than only the most recently constructed instance of an action. The old seven-element latest-node array evicted earlier field nodes when hidden/other instances were constructed. Existing capture evidence shows several field instances being initialized; no later selected draw was established for those earlier identities. The new bounded live table retains each registered instance until its actual type-1 destruction/reinitialization hook retires it. A fixture demonstrates that two simultaneous A guides both select artwork and retiring the second leaves the first eligible. Whether this resolves the owner-visible field guides remains a live acceptance question.

All admitted instances still use the same known parent callers, selector identities, resource-prefix/record hashes, type/vtable, publication read, texture-table binding, source serial/storage and deferred GPU ownership. No new consumer is inferred from resource identity alone. The latest complete Appendage trio behavior is unchanged. Live addresses exist only until retirement; copied commands retain scalar source/artwork generations independently. The 192-record history is not enlarged or recycled. Extra simultaneous instances can consume it sooner, so unlimited-session support is still not claimed.

Two parent wrapper hooks were removed. One existing `0x55ff1c` lookup hook now handles the verified weapon and field groups by their saved parent return addresses. It reads an inspected stack frame during the original call, not a guessed guest object layout or persistent backtrace. Unrelated callers, unaligned/unreadable frames, unsupported selectors, wrong resources and types keep originals. Draw-time current-binding/appearance selection and normal visibility remain unchanged.

A single globally replaced atlas is not supported: current private images contain one licensed physical glyph in the five verified UV regions and transparency elsewhere. Globally substituting them would erase untouched cells. A complete atlas would additionally need per-cell current-binding decisions, original-pixel fallback for unsupported bindings, retained map revisions for copied commands, and verified semantics/materials for every consumer. Neither the directional atlas nor the white-mask atlas shares the first atlas’s UV/material contract. Their live consumers, input identity, refresh and retirement association remain exact blockers, not evidence for a global swap. No such renderer expansion is implemented.

Source inspection also corrected the unimplemented `0x5ae880` candidate: its literal message is `0x13c8` (Modify Avatar), with `mc_FieldHeader` and `mc_TabSet` paths. Issue Orders is a different message row, `0x1341`. It was excluded rather than gaining an artwork-only action assumption.

Upstream and PR feedback were checked before production edits. Upstream remains `feb125c`; the feature stays on merge `3896b5b`, with its reviewed patch table intact. PR #118 contains the owner’s WIP preview and no inline review threads or maintainer change request. No PR update, commit, push, Lua import or naming code change was made. The 36 uncommitted files were hash-preserved under ignored `build/glyph-shared-resources-20261108/preserved-source/` before editing.

The current prepared candidate is `7451906837ba97ec8a05004afa884b0026782cfb40ff2e155e481cac1c7eab95`. CPU fixtures, generation, differential/native checks, content checks and all asset/license checks passed. The coordinated GPU/SDL fixtures subsequently passed, including all 75 image readbacks and fallback/retirement stages; validation layers were unavailable. The coordinated game run is recorded below. Live field A/Target/Board caption correspondence, static refresh and custom-binding acceptance remain pending. The earlier appearance run was owner-approved and closed normally; it is separate evidence.


## Shared-field candidate owner observations (2026-11-08)

Candidate `7451906837ba97ec8a05004afa884b0026782cfb40ff2e155e481cac1c7eab95` closed normally, exit 0, after 169.62 seconds with 45,945 log bytes. The exact-candidate process scan is empty. Agent logs contain 15 Target R selected-draw requests across ten generations, each with selector 14 and active selector 14 requesting PlayStation R1. This establishes the guarded draw requests, not visible Target-caption acceptance. No field A or Board selected draw request was captured.

The owner's screenshots show colored PlayStation Appendage R1/triangle/cross and weapon Square together. The following remain native and outside accepted coverage: all three on-foot control-diagram pages, Settings' X reset guide, tutorial inline A and L/R tab guides, Soul Challenge B, and the + Main Menu/Battle Commands hints. A screenshot alone does not identify their resource, material or input-consumption paths; these consumers must not be admitted merely because their artwork resembles the verified atlas.

The owner reports slot 1 is in the battlefield and slot 2 can reach someone to talk to. Slot 2 is a possible short field-A acceptance target: approach an NPC, observe Talk, move away/back, then close. No slot-2 test has been performed or authorized by that location clarification. No progression or repeated combat check is needed. These screenshots do not confirm the separate Target R or Board ZL guide.


## Actionable text-icon investigation — November 9, 2026

[Current resource report](prompt-actionable-resources.md) identifies `font_icon.wilay` (368×300 BC7, 48 standard/large records) and the `ST:icon` tag family across HUD hints, menu footers and tutorials. The + Main Menu hint is `FLD_HintList_ms` row 3 (`pls`), Settings reset is `MNU_FooterGuide_ms` row 85 (`x_l`), and Auto-attacking is `MNU_TipsList_ms` row 150 (`a`). These are investigated/unresolved consumers, not new replacement coverage. The opt-in, four-resource type-1 bind/descriptor probe ran during the NPC/menu check and captured zero matching samples. The owner saw native menu/tutorial icons; this bounded result does not establish their rendering path.

Soul Challenge's `02110_judgering_btn` image contains only the glowing circle. The actual B text/node and its input/material route remain unresolved; replacing the circle was rejected. Existing `icon_button02` directional and `icon_button03` white-mask consumers remain unresolved. Control diagrams are optional/deferred; licensed package silhouettes exist, but rebound callouts, page integration and trademark clearance are not established. See the report for excluded selector leads and the exact candidate/checklist.

| Status | Current coverage |
| --- | --- |
| Implemented and visually accepted | Appendage and weapon guides, including colored appearance, Automatic and supported KBM; NPC interaction field A, including Button layout changes |
| Implemented, visually unconfirmed | Other field-A captions (Check/Open and related actions), Target R, Board ZL |
| Investigated, unresolved | Inline `ST:icon` HUD/menu/tutorial prompts, + hints, Settings reset, Soul Challenge B, directional/white-mask resources |
| Optional/deferred | Premade controller diagrams and binding-aware callouts |


### NPC interaction acceptance — November 9, 2026

Owner confirms the NPC interaction field-A prompt works on approach and updates appropriately with Button layout. Selected-draw logs show active selector 7 requesting translated artwork. Mark this NPC interaction instance visually accepted; other field-A captions, Target R and Board ZL remain unconfirmed. Gameplay Tips X/L/R/inline A remain native in the supplied screenshot. The scoped resource probe was armed and the completed recording has zero matching observations. Normal window close and exit 0 were verified: 186.08 seconds, 39,699 log bytes; no candidate process remains. Evidence: `build/glyph-actionable-20261109/completed-run-manifest.json`.


## Inline text-icon replacement candidate — November 9, 2026

Implemented in the trial branch and covered by automated tests; **not owner-accepted**. Details, tables and checks: [inline text route](glyph-inline-text-route.md).

| Status | Prompts |
| --- | --- |
| Implemented, automated checks only (awaiting owner look) | A, B, X, Y, L, R, ZL, ZR (small and large) in `ST:icon` text: menu bottom hints and Control Guide (`MNU_CtrlInformation_ms`), large footer guides (`MNU_FooterGuide_ms`, including Settings reset X), Gameplay Tips (`MNU_TipsList_ms`), field hints (`FLD_HintList_ms`), `menu_program_ms`, common telops, option captions |
| Native, layout-based prompts (owner-observed, not covered) | Tab L/R, page-turn R, `Details ZR`, popup `A Close`, `X Check Control Guide`; no text row exists for them, the route into their textures is not traced |
| Owner-observed working | Tips text, field hint pop-up, Map footer with PlayStation/Xbox, layout changes, Standard and Colored; Keyboard & Mouse, Automatic and reopen behaviour not yet accepted |
| Left native on purpose | `+`/`-`, D-pad, sticks, power/home/capture, compound symbols, premade tutorial images, unbound/shared/tileless bindings, Nintendo controllers under Automatic |
| Unchanged | Accepted HUD guides (Appendage, weapon X, NPC interaction A) and the unconfirmed Target R / Board ZL |
| Known limits | Colored tint through the text material is unverified; style changes reach the next layout (reopen the page); 24 signatures per session; atlas reload falls back to native |


## Coverage inventory after the layout/D-pad/stick pass — November 9, 2026

Three classes only. "Accepted" means seen working by the owner; "unconfirmed" means implemented and automatically tested.

| Class | Prompts |
| --- | --- |
| **Implemented and owner-accepted** | HUD guides: Appendage, weapon X, NPC interaction A. Inline text buttons A/B/X/Y/L/R/ZL/ZR (Gameplay Tips, field hints, Map footer) with PlayStation, Xbox, Button layout and Standard/Colored |
| **Implemented, unconfirmed** | Text: `+`, `-`, D-pad directions and groups, sticks, stick presses; keyboard stick and D-pad groups as key clusters (right stick arrow keys, left stick WASD, D-pad 1-4; page-turn included; owner screenshots showed these native before the cluster tiles); Keyboard & Mouse and Automatic for text. Mini-map hint `R + D-pad` (layout `01030_guide_*`, white-mask image `icon_button03`, trial-12): implemented from offline evidence, not seen in a game log yet. Keyboard Shift tile for the default B. Layout widgets drawn from `icon_button`: tab L/R, page-turn (right stick), `A Close` popups, `Details ZR`, `Check Control Guide`, `R + D-pad` widget, in the same symbols. Stick-direction images (`icon_button02`, Control Guide pages). Other field-A captions, Target R, Board ZL (earlier) |
| **Implemented after owner testing (November 10, 2026)** | The mini-map `R + D-pad` hint (`01020_guide`), the Soul Challenge `B` (`02110_button`) and the held field-action button (`03020_button_hold`): single baked images recognized by content ([route notes](glyph-inline-text-route.md)); owner-confirmed on the pad styles and Keyboard & Mouse. |
| **Unresolved or unsupported** | `common_button_red_a` (red-ringed A image), the `icon_button03` white masks (no consumer seen), button glow, premade tutorial pictures, power/home/capture; on keyboard: a stick or the D-pad whose keys are not the arrow keys, WASD or 1-4 in their own positions (native); unbound/shared bindings stay native by design; Automatic with a Nintendo controller native by design |

Not complete: the known unsupported prompts above stay native, each with its reason. Whether every layout widget reaches the verified path is
only established by the owner test (the log lists the panes reached).
