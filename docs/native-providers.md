# Optional native providers: lifecycle v1.0

This experimental interface loads an explicitly trusted native library for an enabled Lua package and creates an idle instance. ABI 1.0 supports lifecycle fixtures only. The explicit [ABI 2.0 service extension](provider-actions.md) adds typed commands, copied status/results and Lua-driven Mods actions; it does not reinterpret the 1.0 contract or add networking/gameplay access. You only need a provider when your mod needs compiled work outside the restricted Lua API, such as a separately maintained networking library. Ordinary settings, buttons and status text need only Lua. A provider does not add game-state or gameplay APIs.

## Package layout and activation

```text
mods/
  enabled.txt
  providers-enabled.txt
  example/
    mod.ini
    main.lua
    provider.ini
    native/
      linux-x86_64/provider.so
      windows-x86_64/provider.dll
```

`enabled.txt` must enable a valid Lua package `example`. Native loading additionally requires `example:example.native` in `providers-enabled.txt`. A catalog entry alone is never native authorization. The native list is bounded to 2048 bytes / 16 associations. Duplicate owner/provider IDs or invalid list syntax reject the complete native activation list. At most four eligible associations are accepted per process; exceeding that cap rejects the native set before mapping anything. Blank/comment lines and CRLF are supported; otherwise no whitespace trimming. IDs use the existing lowercase package alphabet, up to 64 bytes. Missing/disabled/rejected Lua owners never load providers.

A strict `provider.ini` has these fields:

```ini
schema=1
id=example.native
owner=example
abi=1.0
required=0
linux-sha256=<64 lowercase hex characters for provider.so>
windows-sha256=<64 lowercase hex characters for provider.dll>
```

The current platform's hash is required; the other platform's hash is optional. Unknown/duplicate keys, NULs, bad IDs, negative/overflowed capabilities and owner/provider association mismatch reject this provider. Maximum metadata size is 4096 bytes, maximum binary size 32 MiB. `required` is a decimal uint64 capability mask. V1 defines no networking or gameplay capability bits; fixture-specific bits only exercise compatibility checking. No arbitrary library paths, archives, discovery scans or transitive provider associations exist.

The installation root is an explicit trusted absolute path. Each fixed child component is validated, rejecting links/reparse points and special files, checking the opened handle and bounding reads. Linux loads the exact validated file descriptor through `/proc/self/fd/N`; that descriptor and the mapped module remain alive until process exit. Windows holds the validated disk-file handle denying replacement and loads the checked absolute UTF-16 path. Trusted installation ancestors and no concurrent installer mutation remain assumptions; this is not an installer or a hostile-filesystem sandbox.

## Native trust and dependencies

Native code can crash, hang or corrupt this process. Hashes identify package bytes; they do not authenticate publishers or constrain code behavior. Constructors/DllMain execute **before** ABI negotiation and must remain inert in production. Only private test libraries use constructor/destructor markers.

Linux v1 accepts little-endian x86-64 ELF shared objects whose direct dynamic dependencies are limited to `libc.so.6`. It rejects RPATH/RUNPATH, auxiliary/filter/audit imports and malformed program/dynamic/string bounds before mapping. This deliberately requires private C/C++ runtimes/dependencies to be statically linked or eliminated; the C++ fixture is compiled without exceptions/RTTI and linked with the C driver, with no C++ runtime dependency. Supported environment is a trusted glibc system with resident libc resolved under `/usr/lib`, `/usr/lib64`, `/lib` or `/lib64`; glibc and its OS loader are trusted, not recursively treated as mod packages. `LD_LIBRARY_PATH`, `LD_PRELOAD` and `LD_AUDIT` make native activation fail. This cannot undo code injected before host startup or provide ELF symbol isolation. `/proc` must be mounted. `RTLD_NOW | RTLD_LOCAL` is used; providers are not exported globally. [Linux loader behavior](https://man7.org/linux/man-pages/man3/dlopen.3.html).

Windows source accepts x64 PE DLLs, validates section/import bounds and rejects delay imports. Imports are restricted to the OS networking library `ws2_32.dll`, `kernel32.dll`, OS `ucrtbase.dll` / `msvcrt.dll`, and `api-ms-win-crt-*` contracts. DLL name characters cannot contain paths. Loading uses an absolute UTF-16 path with `LOAD_LIBRARY_SEARCH_SYSTEM32`; there is no application/CWD/PATH or private-DLL fallback or process-global search change. Existing system runtime imports/forwarders are trusted OS infrastructure. [Microsoft loading flags](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw) and [disk-file type validation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype) informed this implementation. Windows execution remains untested.

The existing general `plat_lib_open` consumers are unchanged. These checks live in a mod-specific helper so another subsystem's loading policy is not altered. This pass intentionally supports less than a general plugin dependency resolver. The OS ws2_32.dll import is already allowed on Windows. Any additional runtime dependency needs explicit policy review; it does not become allowed just because another library can load it.

## ABI contract

Use [runtime/mod_provider_abi.h](../runtime/mod_provider_abi.h) from C11 or C++17. The sole unmangled exported function is `xcx_provider_query_v1(uint32_t major, uint32_t minor, XmpApi *out)`. Provider definitions use `XMP_EXPORT`, `XMP_CALL` and C linkage. Initial ABI requires exact major 1/minor 0 and exact struct sizes; future expansion must be versioned and reviewed, not appended silently.

| Type | Bytes on tested Linux x64 | Purpose |
|---|---:|---|
| XmpInfo | 176 | ABI/platform/architecture, ID/build ID, capabilities, reserved bytes |
| XmpCreate | 112 | ABI, host owner/generation, required mask, copied package ID |
| XmpApi | 48 | Version header and four lifecycle function pointers |

Natural alignment and little-endian x64 are required; no packing, `bool`, STL, exceptions, ImGui, engine/CPU objects, raw guest pointers, CRT-owned handles or allocation ownership cross the ABI. Reserved/padding bytes must be zero. All buffer sizes/IDs/version fields and required functions are checked before create. Inputs are borrowed for the call only; provider must copy anything retained. Outputs are caller-owned. Provider and host free their own allocations. There are no provider-to-host callbacks and no networking-thread Lua entry path.

Functions are `describe`, `create_idle`, `request_stop`, `join_destroy`. Query/describe start no workers. Create returns a nonzero provider-owned uint64 token on success and may own an inert worker; it must not start networking. A failure returns token 0 **after** complete partial-resource rollback. A provider that returns a failed-create nonzero token violates the contract; the host retains it for conservative stop/cleanup, rather than freeing live state. Native violations without a usable token cannot be repaired by the host.

Stop is nonblocking/idempotent. Join with `timeout_ms=0` must promptly return `XMP_BUSY` until quiescent, then `XMP_OK` only after every worker/resource is gone and the instance destroyed. Busy retains all state; other errors never authorize free. Calls on invalid/destroyed instance tokens return `XMP_INVALID`. A provider cannot retain stack pointers or access host data asynchronously. No callbacks are supplied at all.

## Host ownership and cleanup

All loader calls run serially in one control domain: the existing mod worker in the game, or the main caller of a standalone harness. No provider function runs under coordinator/UI/input locks. The internal host token combines owner, loader generation and slot; a reserved field must be zero. Stale/cross-owner/invalid tokens cannot stop another instance. Repeating stop on the same retired token is harmless until the context is freed; tokens are unusable after context lifetime. Generation is process-monotonic, with overflow rejected. This internal token is not a Lua API or a native pointer.

Startup validates all metadata/images before any mapping, then queries/describes/creates eligible idle instances before associated Lua setup. Individual provider rejection leaves unrelated ordinary Lua packages working. ABI 1.0 has lifecycle diagnostics in the log. ABI 2.0 also exposes copied availability, command descriptors and status through `host.provider.status()` and the Mods page; see [commands and status](provider-actions.md). Lua init/event failure retires its provider after the protected call unwinds. Normal shutdown requests stop for all providers, then joins in reverse association order **before any Lua shutdown callback or state close**; Lua dependency cleanup remains reverse initialization order. Partial Lua setup/OOM follows the same provider-first retirement path.

The join coordinator polls with 1 ms sleeps and a shared five-second production deadline outside locks. A BUSY timeout/error retains live state, refuses context free and cannot claim successful Lua cleanup. The real runtime logs fatal cleanup failure and aborts before closing Lua states. Standalone timeout tests use a shorter compiled deadline and a supervised failure process. A native call that ignores its prompt-return contract and never returns cannot be preempted by this deadline: the external test runner kills/reaps that fixture process. No detached workers, native crash recovery or game-wide watchdog was added.

Every successfully mapped library stays mapped until process exit, including missing-export/ABI-rejected/create-failed modules. The four-module process cap includes those rejected mappings. The same ID/hash may be reused by serial fixture contexts, but changed bytes for an already mapped provider ID are rejected until process exit. There is no `dlclose`/`FreeLibrary`, runtime unload/reload or in-process replacement.

Close the whole process before replacing only package metadata/binaries and restart. On a supported ABI, provider-only updates need no executable relink or generated-code work. The host has been tested with replaced inert provider builds across process restarts. Co-op supplies its own provider and networking in its separate repository. Never include profile/settings/log directories in package replacement operations.

## Tests and platform status

Run `python3 -B tests/providers/check.py` and `python3 -B tests/providers/check.py --sanitize` on the tested Linux toolchain. They build both host and libraries consistently, assert header layouts from C and C++, exercise malformed/absent/disabled packages, two independent providers, failures, tokens, partial rollback, delayed/hung cleanup, retention, Lua ordering and script-only coexistence. Everything uses disposable standalone packages; no game launch or owner naming/settings repeat is needed.

These source-checkout test scripts are not installed beside the game. For native Windows, install MSYS2 UCRT64 Clang and Python, then run `powershell -File tests/providers/windows-check.ps1`. This builds/runs the same lifecycle/package tests and C/C++ layouts without game data. `-Sanitize` additionally requires a supported native sanitizer runtime. Return the complete log; unavailable sanitizer support must be recorded, not counted as a pass. Native Windows DLL imports/search, reparse/junction/Unicode/long-path behavior and toolchain CRT compatibility remain release gates. Linux/Wine/cross-compile evidence does not satisfy them. The Linux pthread coordinator test is skipped on Windows; standalone native lifecycle tests still run there, with full game-coordinator acceptance pending separately.

A provider that reports failed creation while returning a nonzero instance violates the ABI. Its token is retained only for stop/join cleanup; the command service stays faulted and unavailable. Lua settings and unrelated providers remain usable.
