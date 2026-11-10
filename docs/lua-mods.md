# Lua source mods — initial Linux interface

You can enable unpacked Lua packages that use the logging/lifecycle interface below. Each package runs in its own Lua state. F1/Esc → Mods displays host-owned package status and basic script-declared settings. Networking, game-state access and gameplay ticks remain unavailable. Native Windows acceptance is pending.

## Package and activation

Put packages in `mods/` beside the executable, or set `XCX_MOD_DIR` to an explicit absolute installation directory. Packages must remain separate from profiles, saved settings and logs. Only IDs listed in `enabled.txt` are executable. Optional `catalog.txt` uses the same bounded ID-list format to display installed but disabled packages; disabled source is never read. There is no directory scan.

```text
mods/
  enabled.txt
  example.status/
    mod.ini
    main.lua
```

`enabled.txt` contains one ID per line. Empty lines and lines starting with `#` are ignored; LF and CRLF are accepted. Whitespace is otherwise significant. Removing an ID disables that package on the next launch. Duplicate IDs are rejected. Limits: 2,048 activation-file bytes and 16 distinct enabled IDs; exceeding the ID count rejects the configuration. Invalid individual IDs do not prevent unrelated packages from loading.

IDs start with lowercase a–z and contain only lowercase a–z, digits, `.`, `_` or `-`, with at most 64 bytes. They are installation identities, not labels or paths. A package must have these files with exactly these names. Linux opens fixed components relative to directory handles, rejecting symlinks and non-regular files. The explicit installation root is trusted; do not update its contents while the process is running. Windows source wiring exists, but its filesystem behavior still needs native acceptance.

`mod.ini` is a strict UTF-8/ASCII key/value format, with no sections, escaping or whitespace trimming. Values shown below are required except `requires`, which may be absent or empty. Optional `name` is a display label of at most 96 bytes. Duplicate/unknown keys, NULs and incompatible values reject the package. Maximum manifest size is 4,096 bytes.

```ini
id=example.status
api=1
language=lua55-basic-v1
version=0.1.0
requires=
```

`id` must match the enabled ID/directory. `version` is three dot-separated numeric components, at most 31 bytes. `api` is exactly `1`; `language` is exactly `lua55-basic-v1`. Required packages are comma-separated IDs in `requires`, without spaces or duplicates, up to 16. V1 has no dependency version ranges: release/package compatibility beyond the host API/language must be documented explicitly by package authors. No downloads or inter-mod service bus are provided.

`main.lua` is at most 65,536 bytes and is loaded in text-only mode. Bytecode and native Lua modules are unavailable. The native host validates enabled packages and reads source into owned snapshots before executing any script. Parsing/execution use those bytes, not a later filesystem read. Updates become visible after process restart.

## Lifecycle API

```lua
return {
  init = function()
    host.log('initialized ' .. host.id)
  end,
  events = {
    ['host.started'] = function(event)
      assert(event.api == 1 and event.phase == 'startup')
      host.log('host startup event received')
    end
  },
  shutdown = function()
    host.log('shutdown')
  end
}
```

All three callback fields and the `events` table shown above are required. Unknown event names are rejected. `host.id` is the package ID; `host.api` is `1`. The host retains each callback in an owner-scoped registry reference. Changing the original descriptor later does not replace a registered callback. Shutdown is captured before other fields so partial discovery/init failures can still clean up when that function was captured successfully.

`host.started` is delivered once after mod initialization, before guest boot. Its API number and phase are copied into a fresh per-mod Lua table. Mutating it affects only that state. It means that the host mod layer started, not that the game world, renderer or a guest thread is ready. It is not a gameplay update callback.

`host.log(string)` accepts at most 256 bytes, with 16 printed messages per mod lifetime. The host adds identity/phase and sanitizes control characters. Oversized messages fault the caller; excess message count is suppressed. Errors are reported individually with bounded text. No arbitrary printf arguments or private engine objects cross this interface.

Initialization order is topological, with lexical ID tie-breaking. Missing/disabled/rejected dependencies and cycles quarantine affected packages, preserving unrelated valid ones. Failed initialization prevents dependent startup. A later event fault prevents that mod and its dependents from further event delivery. Cleanup still runs in reverse initialization order, including captured cleanup after partial initialization. No callback retry or transactional rollback is promised.

## Language profile

The game statically embeds Lua 5.5.1 with the recorded upstream fixes in `deps/lua/`. Compared with the tiny standalone PoC, this profile adds a reviewed subset of standard facilities:

| Group | Available names |
|---|---|
| Base | `assert`, `error`, `ipairs`, `next`, `pairs`, `select`, `tonumber`, `tostring`, `type`, `_VERSION`, `_G` |
| `string` | `byte`, `char`, `len`, `lower`, `reverse`, `sub`, `upper` |
| `table` | `concat`, `insert`, `pack`, `remove`, `unpack` |
| `math` | `abs`, `ceil`, `floor`, `max`, `min`, `sqrt`, `huge`, `maxinteger`, `mininteger`, `pi`, `tointeger`, `type` |

String method lookup uses the same restricted string table. Pattern operations, `dump`, `rep`, `table.move`, sorting and random facilities are not exposed. There is no `os`, `io`, `package`, `require`, `load`, `loadfile`, `dofile`, `debug`, `coroutine`, `collectgarbage`, `pcall`, `xpcall`, metatable API or native module loader.

The bootstrap opens libraries only inside a protected call and installs whitelisted tables before script execution. Lua's math bootstrap creates internal random-state userdata without a finalizer; no userdata or finalizer construction facility is exposed to scripts. String metatable setup is host-owned. The allocation-failure suite covers this larger bootstrap; adding libraries or user finalizers requires another protection/closure audit.

## Execution and lifetime

All Lua calls run on one dedicated host thread. The game/render/input threads do not enter Lua. Package snapshots are owned by the host until the worker has finished. Callback references remain owned by their state until close; the host closes states only after event delivery stops. No Lua call occurs while the coordinator lock is held.

Experimental limits for this initial interface:

- 1 MiB retained Lua payload per mod, including parser/bootstrap allocations. Shrunk blocks remain charged. Native stacks, allocator headers, snapshots and logging buffers are outside that payload count.
- 20,000 Lua instructions per protected call, in 100-instruction hook intervals.
- 25,000 aggregate instructions per event-dispatch batch, including a 100-instruction reservation per callback for hook tails. Unstarted recipients defer to another worker batch after a 1 ms sleep. A callback interrupted at aggregate exhaustion faults; its partial effects are not rolled back. The settings queue holds 32 actions, coalesces by mod/field, and processes at most eight per batch. Queue overflow rejects the new action visibly. Unstarted actions defer.

The instruction hook cannot preempt parsing, garbage collection, native library work, allocation or blocking logging. The limits are not hard wall-clock/RSS limits, a complete security sandbox or crash recovery. Pure Lua source portability also depends on the supported language/API profile; it does not make native providers portable.

Ordinary interactive game launch starts this layer after early guest-address reservation and settings/log setup, before `rt_boot`. Selftest, explicit probe/boot modes, screenshot and timed-test launches do not load packages. With no valid enabled packages, no mod worker or Lua states are created.

Normal `plat_exit(0)` stops delivery, cancels unstarted delivery, wakes/joins the worker, runs bounded cleanup, closes states and releases snapshots. Concurrent shutdown callers are serialized. A stop request originating on the mod worker never waits for itself. The exposed Lua API cannot request process exit. Nonzero/fatal exit paths bypass mod cleanup so crash reporting never waits on the mod lock. Forced termination and arbitrary native crashes do not guarantee cleanup. A blocked native call can delay graceful shutdown.

## Update and platform boundaries

Close the process, replace only package files and restart. Supported script-only updates require no executable relinking, generation or native compilation. New host services require a native rebuild; new verified game hooks may additionally require regeneration. No hot reload, archive installer or updater exists.

Keep profile/save/settings/log paths out of package update operations. Existing save-flow tools are unchanged. Windows source/build wiring is retained, but a Linux build, cross-compile or Wine run cannot establish native Windows acceptance. Python is used by development/build tests, not by the running mod loader.

## Settings interface v1

Each mod may call `host.settings.register` once during `init`. Registration validates and copies the page; later Lua-table mutation cannot change it. A page has `version=1`, stable `id`, `title`, and 1–8 `fields`. IDs follow the package-ID alphabet and 64-byte limit. Labels/titles are valid UTF-8, at most 96 bytes, without control characters. Duplicate field/option IDs are rejected.

```lua
host.settings.register {version=1,id='preferences',title='Preferences',fields={
  {id='hints',label='Show hints',type='bool',default=true},
  {id='amount',label='Amount',type='integer',min=0,max=100,default=25},
  {id='theme',label='Theme',type='enum',default='calm',options={
    {id='calm',label='Calm'}, {id='bright',label='Bright'}}}
}}
local hints = host.settings.get('hints')
```

Integers must be Lua integers within ±1,000,000, with valid min/max/default. Booleans require actual Lua booleans. Enums have 2–8 options and a default matching a stable option ID. `host.settings.get(field_id)` reads only the calling mod's current typed value; unknown fields fault the caller. No shared mutable table, engine pointer or widget object is returned.

The optional descriptor callback `events['settings.changed']` receives a fresh copied table `{page=page_id, field=field_id, value=typed_value}`. Enum values are symbolic IDs, never display labels or positions. This runs on the existing dispatch thread under the same instruction/memory limits. The host applies and attempts to save the validated value before the callback; callback failure does not roll it back. A fault quarantines the mod and dependents, retains the page layout and diagnostic, and disables its settings.

Rendering copies native snapshots and queues actions; it never enters Lua. Actions carry generation/session tokens and stable mod/field IDs. Leaving Mods, closing the overlay, entering text input, losing the renderer or shutting down cancels unstarted actions. An already executing action may finish. Wait for the save acknowledgement before leaving the page. Enabled-package changes require restart; there are no enable controls or hot unload.

Settings live beside the executable in `mod-settings/<mod-id>.ini`, independently of an overridden package root, profiles and account IDs. The bounded version-1 format uses typed values and stable enum IDs. Invalid known types/ranges, duplicates or malformed files discard the whole stored set and use declared defaults, with a visible note; startup does not overwrite the file. Missing fields use current defaults, existing valid values survive default changes, removed fields are ignored and dropped on the next successful save. Renaming a field creates a new identity. Enum reordering preserves the symbolic choice.

Each save writes a temporary file and atomically replaces the previous file. Failure preserves the previous file, retains the session value and displays a warning. Close the process and replace only package files when updating scripts: settings, profiles and logs remain outside that operation. See [compatibility and platform limits](mods/04-compatibility.md) for validation and platform limits.


## Settings failure recovery

Missing settings use defaults silently. Unreadable, unsafe or oversized files use defaults with a bounded warning; the host reads only validated regular disk files without following links. Rejected stored values are not overwritten at startup.

A failed save keeps the prior file and marks the current page unsaved. **Retry save** appears only for an unsaved page. It submits the current value to retry writing the whole page; success clears the unsaved state. Retrying an unchanged value does not deliver `settings.changed` again. Faulted pages retain the layout but cannot retry. Closing the page cancels an unstarted retry under the same rules as other settings actions.

If the native mod worker or runtime cannot start, eligible packages show Faulted with a restart diagnostic; disabled and incompatible package statuses remain unchanged.
