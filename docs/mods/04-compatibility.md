# 04 · Compatibility and troubleshooting

## Versions and capabilities

The package manifest requires `api=1`, `language=lua55-basic-v1` and a three-component numeric package version. Settings schema 1 and optional action declarations use the existing Lua API 1. Menu registration has its own explicit version 1 and location `online`. Native lifecycle ABI 1.0 and command/status ABI 2.0 are distinct exact-version C interfaces; neither silently falls back. Provider manifest schema 2 requires the generic host-service and actual game version when you select that schema. Schema 1 does not carry those compatibility fields. Source-only packages have no native ABI/build requirement.

| Capability | Status in this candidate | Boundary |
| --- | --- | --- |
| Source package discovery/activation, bounded Lua startup/events and shutdown | Implemented/tested on Linux; experimental API | Restricted per-owner state, startup loading |
| Typed settings, Retry save, copied action/status pages | Implemented/tested on Linux; experimental API | F1/Esc Mods; eight fields/four actions |
| Native provider lifecycle and typed asynchronous commands | Implemented/tested on Linux; experimental trusted native ABI | Explicit activation; platform binaries; no hot unload |
| Online child registration v1 | Implemented/tested on Linux for native row, page opening and Back; experimental API | XCX DE 2.0.0 only; one extra entry; generation opt-in |
| Native Windows runtime behavior | Pending native validation | Portable source/runner is not execution evidence |
| Arbitrary menus/widgets/functions or unrestricted hooks | Unavailable | Needs a separately reviewed shared adapter |
| Actor/player/combat/quest/save manipulation | Unavailable | UI/events do not expose these capabilities |
| LAN/internet Co-op or gameplay synchronization | Unavailable in the loopback package | Transport-connected is not gameplay-ready |
| More menu locations, independent concurrent controller pages | Proposed, not implemented | Requires contracts and capacity/lifetime validation |
| Hot reload/unload and in-game activation toggles | Unavailable | Edit activation while closed, then restart |

The Lua implementation is 5.5.1 with four recorded upstream fixes applied to a build copy; the language profile exposes selected base/string/table/math operations. `io`, `os`, `package`, `require`, `debug`, dynamic loading, arbitrary filesystem/network access and coroutine services are excluded. Each package has bounded memory/source/instructions and protected callbacks. The host dispatches startup, settings, UI actions and provider events; there is no frame/gameplay callback. Host logs are bounded to 16 messages per state, so prefer state transitions over counter spam. See [Lua reference](../lua-mods.md) for exact limits and [provider/action reference](../provider-actions.md) for aggregate fairness.

## Troubleshoot

- **Not listed:** check the selected absolute mods root, catalog/enabled IDs, folder/manifest ID agreement and source files. Discovery is not permission to execute.
- **Disabled:** add the stable package ID to enabled.txt and restart. A catalog line alone does not enable it.
- **Incompatible/faulted:** read the bounded page diagnostic and instance log. Check API/language/version/dependencies; a faulted owner stays quarantined until restart.
- **Save failed:** session value remains; Retry save repeats persistence. Restore a writable safe settings directory. Do not overwrite rejected startup settings manually as a hidden recovery step.
- **No Online shortcut:** check version/location, page ID and init timing, then unsupported/full/unavailable. Only one slot exists; F1/Esc Mods still works. The executable must have the checked generated adapter.
- **Provider unavailable:** enable the Lua package and its separate owner/provider association, verify manifest hashes/ABI/platform requirements and restart. Native libraries are trusted code, not sandboxed Lua.
- **Wrong page or stale input:** close/reopen normally and inspect the owner/page IDs. Do not add a private input stack or retain guest controller pointers.

Linux tests and owner observations do not prove Windows support or cross-platform play. Keep your test installation separate from your normal one. The examples don't update an existing installation or replace its profile.
