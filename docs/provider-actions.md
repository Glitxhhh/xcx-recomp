# Provider commands and Mods actions

Your Lua package can add transient action buttons to F1/Esc → Mods, send typed asynchronous commands to its explicitly activated native provider, and show copied status and results. Ordinary source-only Lua mods can use actions and status without native binaries. This experimental bridge has been tested with inert C/C++ services; it does not add networking or gameplay access.

## Register actions

Keep your existing package and settings declaration. Add at most four actions with stable IDs, labels and an optional `provider=true` gate. Supply a `ui.action` handler. An actions-only page may use `fields={}`. Settings schema and Lua API remain version 1; actions are an optional declaration extension.

```lua
return {
 init=function()
  host.settings.register {
   version=1, id='work', title='Work', fields={},
   actions={{id='run', label='Run work', provider=true}}
  }
 end,
 events={
  ['host.started']=function() end,
  ['ui.action']=function(e)
   if e.action=='run' then
    local request,reason=host.provider.submit('success')
    host.ui.status(request and 'Accepted' or reason)
   end
  end,
  ['provider.event']=function(e)
   if e.kind=='result' then host.ui.status(e.result..': '..e.message) end
  end
 },
 shutdown=function() end
}
```

The example's `success` command belongs to the inert test service. Query `host.provider.status().commands` for the commands your associated provider actually describes. Production host code contains no fixture opcodes. Action IDs follow the settings ID rules, labels are bounded to 96 bytes, and action status is UTF-8 without ASCII control characters, at most 256 bytes. Actions are never saved or replayed after restart.

`host.ui.status(text)` copies a read-only line. `host.ui.enable(action_id, boolean)` controls your own declared action. Provider-gated buttons additionally require an available provider with zero pending requests. Other script-only buttons remain usable without a provider. A script without native files needs no build step.

## Submit and receive work

`host.provider.status()` returns a fresh table containing `available`, `message`, `service`, `pending`, `handle`, `capabilities` and `commands`. Each command has `id`, `type` (`none`, `bool` or `integer`), `min` and `max`. Capabilities are a 16-digit hexadecimal string, including high bits. Handles and requests use three colon-separated 16-digit hexadecimal components: generation, owner and request. A provider handle has request zero. Treat both strings as opaque identities; they never pass through floating-point numbers.

`host.provider.submit(command_id [, value])` implicitly targets only your package's provider. It requires an initialized healthy owner and a `provider.event` completion handler. NONE takes no value, BOOL takes a Lua boolean, and INTEGER takes an actual Lua integer in the descriptor range. Unknown commands, invalid IDs and wrong types/ranges are script errors handled by the existing protected-call fault policy. No arbitrary native handle, opcode, socket or address is accepted from Lua.

Success returns a request string. Failure returns `nil, reason`: busy means capacity is exhausted; rejected means the provider declined before acceptance; failed means submission failed; unavailable/absence/disabled/incompatible/faulted provider returns availability feedback. A stopped owner or missing result handler cannot submit. Rejection reserves no lasting credit and produces no terminal event. Accepted commands can later finish with `ok`, `failed` or `rejected` results.

`provider.event` receives a newly copied table. Both kinds contain `handle`, `kind` and `message`. A `result` additionally contains `request` and `result`; a `status` is the latest coalesced text. There are no native-to-Lua callbacks. All Lua calls run on the existing dispatch worker inside protected calls with allocation and instruction limits.

Closing the overlay, changing tabs/mod selection, starting text entry or losing the renderer cancels only unaccepted UI intents. Accepted work continues and can finish while the page is closed. Owner script faults and process shutdown instead revoke outstanding delivery and stop/join the provider before Lua shutdown or state closure. Shutdown cannot submit new work.

## Native ABI 2.0 reference

Use [mod_provider_service_abi.h](../runtime/mod_provider_service_abi.h) and the unchanged [lifecycle types](../runtime/mod_provider_abi.h). Declare `abi=2.0` in `provider.ini` and export `xcx_provider_query_v2`. ABI 1.0 still uses its original query, structures and exact validation. There is no fallback or silent minor extension: unsupported versions, wrong sizes, reserved fields, padding, identities, platform/architecture or required capabilities reject the provider.

| Structure | x86-64 size | Contents |
| --- | ---: | --- |
| XmsCommandSpec | 96 | Provider-defined opcode, NONE/BOOL/INTEGER type, int64 range, stable ID |
| XmsInfo | 624 | Size/version/platform, uint64 capabilities, provider/build/service IDs, four specs |
| XmsCommand | 48 | Size/opcode/type, owner/generation/request, int64 value |
| XmsResult | 296 | Size/result, owner/generation/request, copied message |
| XmsStatus | 296 | Size, owner/generation/revision, copied text |
| XmsApi | 72 | Header and seven function pointers |

The API supplies describe, create_idle, request_stop, join_destroy, submit, poll and status. Lifecycle reuses XmpCreate with major=2. Buffers are borrowed for the call only; the receiving side copies them. Both sides own and free their own allocations. All host control calls are serialized on the mod worker outside UI/input/coordinator locks. Provider workers own asynchronous execution; submit must copy promptly and never wait for completion. No host callback, C++ object, engine/ImGui object or guest pointer crosses this ABI.

Submit returns XMP_OK only after acceptance and owes exactly one terminal result. BUSY, INVALID or FAILED accepts nothing. Poll returns XMS_EMPTY when empty, otherwise XMP_OK and a fully initialized record. Terminal results allow OK/INVALID/FAILED. Status returns a full copied record with nonzero, non-maximal, monotonic revision; identical revisions must retain identical text. Strings must terminate within their fixed buffer and contain valid UTF-8 without ASCII controls. Reserved/padding fields must be zero. Command/provider/build/service IDs use the existing bounded lowercase ID grammar. Integer ranges are limited to −1,000,000 through 1,000,000. There are at most four command specs.

Each provider has 32 outstanding credits, reserved before submit and retained until terminal dispatch. With four providers this bounds outstanding host work to 128. FIFO side-effect commands never enter the coalescing settings lane. The separate UI intent FIFO holds 32 entries and explicitly rejects overflow. Latest status has its own coalescing slot. The worker polls at most four terminals per provider per tick and status at most ten times per second. Settings, UI intents and provider deliveries rotate fairly, with at most eight deliveries per batch, 20,000 instructions per callback and 25,000 in aggregate. Status and result selection alternates when both are ready, and event owners rotate.

Duplicate, unknown, malformed, stale or cross-owner records fault and retire that provider. The host joins its worker, keeps already queued valid terminals and synthesizes one failed terminal for every other accepted outstanding request. The Lua owner can show the fault and continue ordinary settings. A Lua owner fault instead cancels delivery and native work; no post-teardown callbacks are permitted. Native stop/join retains the previous shared five-second shutdown deadline and fatal retention policy: never free live native state if cleanup fails. A provider must honor prompt-call behavior; native code is trusted code, not a sandbox.

## Restart and platform limits

Activation, package association, secure loading, hash verification and dependency restrictions remain as described in [native-providers.md](native-providers.md). Libraries stay mapped until process exit; there is no runtime unloading/replacement. Exit first, update compatible native files and their manifest hashes or Lua/assets, then restart. The host need not relink for a compatible provider-only or script-only update. Ordinary settings remain persisted; transient actions do not replay. A script can deliberately submit from a future startup handler, which is its own policy.

Linux C/C++ ABI, worker, Lua, sanitizer and UI checks are executed. Windows execution is pending. On native Windows with MSYS2 UCRT64 Clang and Python, run `powershell -File tests/actions/windows-check.ps1`, then its `-Sanitize` variant where the sanitizer runtime is available. This runs loader and service/Lua tests without game data. Windows game build, selftest and a coordinated action/close/reopen/quit check remain separate requirements; Linux results do not prove them.
