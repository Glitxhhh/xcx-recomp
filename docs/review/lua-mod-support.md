# Lua mod support: reviewer guide

This is an experimental reusable mod host. It loads explicitly enabled source packages, presents their settings/actions/status in F1/Esc → Mods, optionally associates trusted native providers, and supports one checked native Online row that opens that same host Mods page. Game-native content windows and gameplay APIs are deferred.

## PR base and ownership

Review `feature/naming-and-controller-keyboard` against upstream `master` (`7b2337a6854b3936dc4f567dbb46add8c1eb6559` at preparation). Its two commits are `2cb2ab2` (XCX SDK name-buffer layout) and `d3b69f8` (controller keyboard). The production sources and original test files were reconstructed from the accepted nine-file keyboard snapshot; the historical acceptance document is replaced by the maintained [keyboard guide](../naming-and-controller-keyboard.md). The original snapshot remains preserved separately. Linux owner acceptance covers the tested PlayStation candidate; other controller families and native Windows remain pending.

Review `feature/lua-mod-support` **against `feature/naming-and-controller-keyboard`**, not master. This makes keyboard code a prerequisite rather than relabeled Lua changes. After the prerequisite lands, compare/rebase against the then-current master in a later authorized step; the keyboard dependency is unchanged. The keyboard test runner gains an explicit Mods-test option in the settings commit; keyboard production behavior is not reimplemented by this PR.

`feature/input-glyph-styles` is independent and was not imported. Co-op transport, provider implementation/binaries, captions/registration declarations and multiplayer policy stay in xcx-seamless-coop. Recomp contains the generic provider and menu contracts only. Tests contain inert C/C++ service fixtures; those are not a Co-op implementation.

## Review by component

| Commit | Review focus |
|---|---|
| `59f73d9` | Pinned pristine Lua distribution, MIT notices, four raw upstream fixes, deterministic preparation |
| `fd53f06` | Bounded package discovery/explicit activation, restricted per-package Lua states, protected lifecycle and dependency fault propagation |
| `0f4bcc0` | Copied settings snapshots, typed schema, persistence, Mods rendering and input ownership |
| `af95c95` | B1: nonblocking/no-follow regular-file settings reads; unsafe/unreadable versus missing; bounded all-or-default validation |
| `0510d35` | B2: coordinator creation/native allocation failure; fault eligible Starting packages, preserve disabled/incompatible status |
| `4cbf774` | B3: dirty persistence independent of session values/callbacks; same-value retry |
| `2a8f266` | Final persistence contract documentation |
| `1221be8` | Explicit owner/provider association, native file/image/dependency validation, lifecycle ABI and worker cleanup |
| `066c713` | Service ABI 2.0, owner-scoped typed commands, credit queues, copied status/results and Lua/UI bridge |
| `e8e43d4` | Host/game compatibility requirements validated against actual linked metadata |
| `0bdf542` | Init-only registry, checked Online adapter/generator, route to the existing Mods page and cancellation |
| `cfc8792` | Complete first-mod example and task-oriented author guides |
| `74bdd9b` | Confirmed failed-create contract violation: quarantine a live token rather than expose its service |

`ae8f01c` completes documentation and the two executable example regressions. The final review-record commit adds this guide and compact source attribution. Full hashes, imported source hashes and per-component changed paths are in [source attribution](lua-support-sources.json). Earlier intermediate commits represent historical feature stages; the final stack is the review/test target. No claim is made that every historical stage passes the final suite or is an independently accepted release.

Start source review with `mod_package.c` and `mod_runtime.c`, then `mods.c` startup/shutdown. Review the snapshot/session checks in `mod_settings.c` with `ui/mods_page.cpp` and `ui/menu.cpp`. Provider calls are serialized on the mod worker outside host locks; owned asynchronous work stays in provider threads. Inspect `mod_provider{,_platform}.c` and both ABI headers together. Shutdown stops/joins native owners before Lua state teardown. Failure to retire trusted native code is fatal retention, not recoverable isolation. Mapping persists until process exit; arbitrary native code is trusted, not sandboxed.

The checked menu adapter validates game identity/instructions and runtime descriptor shape. It copies live descriptors into owned process-lifetime storage, preserves original rows, and consumes only its own registered row. No Nintendo resource bytes or generated guest functions are distributed. Registration/rendering never calls Lua on guest/render threads; selection queues a copied owner/page request. Unsupported generation, bad owner/page, renderer loss and text-entry priority must remain explicit. There is one global slot and conservative controller epoch cancellation, no runtime unregister/rename or arbitrary menu locations.

## Reconstruction and the narrow correction

The frozen primary source inventory has SHA-256 `834f8141e446e8e38251a7ce6d4058d6e78836b3c8595fe08715760827bb1f6d`; accepted keyboard snapshot identity is `711e2b9e18b8f0fcfb3cc80056ba9e5a4864396c7e39205a27631cdd39799763`. Older feature stages were replayed from their verified frozen sources; B1–B3 used their separately hashed patches. The complete final production and test implementation matches the accepted combination except the explicit corrections below. At the November 7 preparation pin, upstream changed only the cloud-reader patch since the older base; `recomp/patches-upd.txt` was byte-identical to upstream at that pin, SHA-256 `19f322dcdd26adee25bd8e0091a8456a9f861fe1b532e0fee1e33102d7d08191`.

A provider can violate the failed-create contract by returning failure with a nonzero live instance. Before correction, the retained token was also considered a valid command service: a new fixture accepted `success` and completed an action despite failed creation. The correction sets its fault flag while retaining cleanup ownership. After correction, actions are unavailable, pending work stays zero, unrelated Lua/settings/providers remain usable, and normal stop/join destroys the retained worker before Lua teardown. `tests/actions/test_actions.py` contains the failing-before/passing-after regression; no normal success-path behavior changed. No owner gameplay repetition is needed for this malformed-provider startup path.

Documentation changes replace a link to an omitted historical report, explain exact log locations/redirection, supply required lifecycle handlers in the provider-actions example, describe the actual Windows OS import allowlist (including ws2_32.dll), date the upstream-version observation and link author docs from README. New example tests execute the documented Online registration and provider-actions declaration. No other production correction or architectural change is included.

Historical absolute-path audit tools, duplicated full-source prerequisite manifests and old acceptance/research narratives are retained in the external preservation set, not shipped as maintained product documentation. The source attribution here records their relevant identities and actual commit/file slices. Game assets, extracted resources/descriptors, generated guest C, executable/provider binaries, logs, profiles/saves, keys/account data and all native-window research outputs are excluded. Review tracked **and untracked** files; do not use `git add .` on an evidence tree.

## Author entry points and limits

Start at [Lua mods](../mods/README.md), then [First mod](../mods/02-first-mod.md). The checked-in [hello package](../../examples/mods/hello) needs no compiler. The guides distinguish catalog discovery from enabled activation, explain status/errors/logs, settings/actions/status, and the current [Online registration](../mods/03-menu-registration.md). [Compatibility](../mods/04-compatibility.md) separates implemented Linux evidence from experimental, proposed and unavailable capabilities. References: [Lua/settings](../lua-mods.md), [providers](../native-providers.md), [commands/status](../provider-actions.md).

Ordinary Lua packages are portable source within the restricted API/language profile; optional native providers are trusted x86-64 platform binaries with exact ABI and explicit activation. Updates require closing the process and restarting; preserve mod-settings, xcx.ini, profiles/saves and logs. No hot reload/unload, arbitrary filesystem/network/socket/guest-memory API, actors, combat, quests, save manipulation or frame/gameplay callback is exposed. `host.started` means host startup only. A native Online row is implemented; a game-native content page is not.

## Verification and reproduction

Run headless commands serially from the feature root with Linux Clang and Python:

```sh
python tools/check_content.py
python tools/prepare_lua.py
python tests/mods/check.py --cc clang --sanitize
python tests/providers/check.py --sanitize
python tests/actions/check.py --sanitize
python tests/menu/check.py --sanitize
python tests/menu/check_docs.py
```

The pinned preparation verifies pristine/patch inventories without downloading code or altering pristine sources. All 75 pristine distribution files, four raw patch hashes, license and third-party notice are retained. The three PowerShell runners retain CRLF worktree bytes through the existing `.gitattributes`; Git stores their LF-normalized blobs. The source attribution records both hashes for those files. All other recorded Git blobs match the prepared raw source hashes, including pristine Lua and its patches. Raw upstream whitespace is intentional: use `git diff --check <base> -- . ':(exclude)deps/lua/pristine/**' ':(exclude)deps/lua/patches/**'` for authored whitespace checks. Do not normalize pinned dependency files to silence Git warnings.

For native UI checks use a matching existing native build with `python tests/controller_keyboard_check.py --build <build>` and its `--mods` variant, then each printed private executable with `--probe-window` (headless private driver, not a game/GPU test). A normal Linux candidate can run `--selftest` without launching gameplay. Rebuilding the keyboard needs no guest regeneration. Menu generation requires `recomp/gen.py --exefs <owned-exefs> --out <ignored-output> --patches recomp/patches-upd.txt --jobs 2 --mod-menu-hook`; retain ordinary generator/build inputs/options from [building](../building.md). Build native code against the resulting capability header, using at most two jobs for this review. The menu adapter compiles per install against the generated capability header; it is excluded from the generic runtime archive.

Preparation evidence: the reconstructed Lua/settings baseline passed 32 sanitizer tests plus four upstream regression families before the one-flag provider correction (its absent-provider paths are unchanged). After that correction: provider 22 sanitizer tests; actions 20 sanitizer tests including the new failure case; menu 14 tests using the sanitizer-built production host including complete author examples; keyboard 55 and Mods UI 55 headless checks; isolated Linux native relink and selftest passed. The UI tests compiled actual reconstructed menu sources and reused accepted unchanged native/generated objects read-only. This is not a fresh clean full build. The relinked executable SHA-256 is recorded in the source attribution; identical source alone does not guarantee identical binaries.

Unchanged generator/adapter evidence is reused: exactly two generated C chunks differed from current upstream/no-hook output, generated selection/label contracts passed, 211 differential cases over 21 functions had zero disagreements (369 skipped), and 2,000,484 flag pairs at O0/O1 had zero disagreements. No new generation or GPU/game test was run during commit preparation. Existing signedness compiler warnings remain; this is not a warning-clean claim.

Prior coordinated Linux owner checks accepted settings/retry/actions and Online-row opening/Back/normal quit. Co-op loopback Host/Join/Leave/reconnect and provider-only updates were tested separately in its repository; transport connected does not mean gameplay ready. Exact returned-row focus, all-device held-input behavior and every original Online option were not individually owner-confirmed. Those limits do not require repeating accepted gameplay for this reconstruction.

Native Windows acceptance remains a release gate: native build/selftest; real settings disk/reparse/error/retry cases; restricted DLL/dependency behavior; worker/start/stop/fault/queue coverage; script-only and provider packages; UI navigation, text priority, device switching/held input and Online/Back/normal quit. Existing PowerShell runners are useful but do not establish Windows disk persistence or game acceptance. Linux tests, cross-compilation and source portability cannot substitute.

Storage: the owner-coordinated preservation set contains 14 inventories and 1,044 independently verified blobs on a separate filesystem. Read-only Git/object/source checks passed before preparation, with no EIO observed. This does not diagnose the previously reported drive errors. Stop writes on recurrence; do not repair/reset/clean/delete. Older worktrees and the tested combination remain preserved; new local commits do not authorize deployment or publication.


## November 8 release preparation

The release preparation follows `5a4e4bd` above keyboard prerequisite `d3b69f8`; no history was rewritten. At this preparation stage, upstream was `59803d9a40e1a96e69114f47f9a43f9f6bc9cd03`, with rendering, installer and save-flow changes. They were fetched and reviewed for overlap but not imported. This stacked diff is still reviewed against the keyboard branch, not advertised as integrated with current upstream. No keyboard agent was registered in this session; a separate coordination note records the unchanged ancestry and overlapping files.

Release-path regressions reproduced three omissions: a prebuilt-runtime installation still imported the unavailable Lua preparation tool, the Online adapter was compiled into the generic archive without its generated capability configuration, and the public guides/example/Lua license were absent from the package. Local fixes skip Lua preparation when linking an archive, compile the adapter per install, and ship an explicit public file list. The installer requests the checked menu hook for 2.0.0 and invalidates the old lift cache. Installation copies guides, the example, ABI headers and notices to managed paths, preserving mods, mod-settings, xcx.ini and profiles. The Lua dependency and runtime ABI/lifecycle code are unchanged.

Fresh Linux x86-64-v3 and x86-64-v2 archives each compiled 125 runtime/Lua objects at two jobs; all objects were hash-verified. A packaged source tree with no Lua sources/preparation script linked the install-side code and existing unchanged generated objects against that archive. Selftest passed. A private entry around that same native build tested real executable-relative discovery, restart-only activation, action/status, atomic settings writes/reload, disable/re-enable, rejected settings preservation, script replacement and an inert provider's stop-before-Lua order. No game boot, window or GPU test ran. The executable hash stayed unchanged throughout the source/provider installation checks.

Release-copy/installer regressions check file contents, all installed guide links, cache invalidation and protected user files. At this stage the complete native SDK/AppImage assembly had not run: the pinned Steam Runtime/LLVM/Python/.NET release toolchain is not prepared here. The successful local system-toolchain split build is not a SteamOS/glibc-2.31 or packaged AppImage acceptance claim; execution on hardware limited to x86-64-v2 also remains untested. [Native Windows commands](windows-mod-validation.md) and the new source test bundle provide the remaining platform procedure. Native Windows has not been run.

The release preparation is a separate packaging/test/documentation commit. It does not change the runtime ABI or keyboard dependency. Missing author guides are checked during packaging and copied during installation; they do not add another toolchain-availability gate. The original frozen attribution describes the earlier accepted baseline and is retained unchanged. Native Windows and full release-toolchain acceptance remain pending; the PR stays draft.

Final review reran five packaging regressions, 17 installer bookkeeping checks, guide/example consistency, 14 sanitized menu tests and the private installed-runtime check. Discovery/restart, enable/disable, persistence, rejected-file preservation, script replacement and inert-provider cleanup passed with an unchanged private executable hash. The redundant public-guide check in `Layout.missing()` was removed; release-copy coverage and install copy failures protect that contract.

## Final release-toolchain validation

See [release validation](release-mod-validation.md) for exact Linux package/proof commands and native Windows ZIP/source gates. Finalization fetched upstream `feb125c` (Division Rewards change); it was inspected but not merged. The PR has no reported checks, reviews or inline comments and remains draft. Keyboard ancestry and the separate glyph/native-page branches are unchanged.

The pinned Linux release folder and AppImage were assembled locally at `074749c`, using LLVM 23.1.4, the pinned Steam Runtime SDK and two compiler jobs. Both 125-object runtime archives built. The release content scan (7,568 files), glibc-floor check (2.34 for tools/Python), bundled check-only installer, Xft/font checks and both-language installer UI selftest passed under Xvfb with software Vulkan. AppImage extraction and the same checks passed again. All 15 public mod-support files in the extracted package match the source, including the example, ABI headers and Lua license; private/game/user directories are absent. AppImage SHA-256: `63a7b4be334884a9979fd74f888a96aad802679cb02c7753d10f0953acc4f32c`. This is a local validation artifact, not a published release.

Two environment failures were resolved without product-code changes: the container lacked Git for the pinned hactool checkout and `file` for appimagetool. The release prerequisite documentation now names them. No new Lua/provider/settings/menu defect was confirmed.

The extracted package then cold-compiled all 200 generated files plus five install-side/module inputs and linked against its own runtime archive. This reused the accepted generated source read-only, not existing object files. Selftest passed with zero failures. A private entry around the same build passed discovery/restart, enable/disable/re-enable, action/status, real settings persistence, unsafe-file preservation, script update and inert provider-before-Lua cleanup. Its executable SHA-256 stayed `429bebab86ecbeaff8cf0eb722477f6b17db7452754b49abc516b09338ef845e`; the normal executable SHA-256 is `8cd94cbcc0e1827f5ae3ed358c3b3ad4ad6e0efa14001a343a6a785dd97ce3ef`. The game executable imports GLIBC symbols through 2.29; this is not an execution claim on older distributions. The container additionally needed SDL's system `libXss` dependency for the packaged library. The private runner needs `--build-root` set to the package for these relative Ninja paths.

Native Windows build/ZIP/installer/runtime/input acceptance remains pending. No suitable played shader census was found in the inspected worktrees, so the complete `--prove` shader-probe sequence remains pending on a machine with those inputs. SteamOS/glibc-floor and x86-64-v2-only execution remain pending too. No game boot or owner-profile change occurred. Apart from prerequisite and compatibility wording, finishing changes are reviewer/test instructions; production behavior and the keyboard dependency are unchanged. There is no newly confirmed local runtime-code blocker. Maintainers still need to review/integrate the keyboard prerequisite and reconcile the stack with current upstream before merging.
