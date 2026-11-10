# 02 · First mod

This complete example adds a saved checkbox, a transient button and a read-only status line. It uses only portable Lua source.

## Install the files

Create `mods/hello` beside the executable, then copy [examples/mods/hello](../../examples/mods/hello) into it. You can also create `mod.ini` and `main.lua` yourself using the complete text below. Save them as plain UTF-8 text without a byte-order mark (BOM) with those exact filenames; check that your editor hasn't added `.txt`. You don't need game assets, a compiler or a native library.

```text
<instance>/
  xcx                     # xcx.exe on Windows
  mods/
    catalog.txt
    enabled.txt
    hello/
      mod.ini
      main.lua
  mod-settings/hello.ini  # created by settings persistence, outside the package
```

`mod.ini`:

```ini
id=hello
name=Hello workshop
version=0.1.0
api=1
language=lua55-basic-v1
requires=
```

`main.lua` (also available as [source](../../examples/mods/hello/main.lua)):

```lua
local clicks=0
return {
 init=function()
  host.settings.register {
   version=1,id='preferences',title='Hello workshop',
   fields={{id='hints',label='Show hints',type='bool',default=true}},
   actions={{id='greet',label='Say hello'}}
  }
  host.ui.status('Choose Say hello.')
 end,
 events={
  ['host.started']=function() host.log('Hello workshop loaded') end,
  ['settings.changed']=function(e)
   host.ui.status('Show hints: '..tostring(e.value))
  end,
  ['ui.action']=function(e)
   if e.action=='greet' then clicks=clicks+1;host.ui.status('Hello! Clicks this run: '..tostring(clicks)) end
  end
 },
 shutdown=function() end
}
```

The init function registers your page; the `ui.action` handler increments a session-only counter, and `settings.changed` reports the new checkbox value. Required init/shutdown functions and the startup event are supplied.

Create `mods/catalog.txt` and `mods/enabled.txt` if they don't exist. Add a line containing only `hello` to each file, keeping any other entries. Close the whole process if it's running, then start it again. Open F1/Esc → Mods → **Hello workshop**. Click **Say hello**: the line changes to `Hello! Clicks this run: 1`. Change **Show hints** and wait for Saved. Close/reopen the overlay: the setting remains. Restarting loads the saved checkbox and resets the transient click counter. If saving fails, the existing Retry save control retries persistence without repeating the change callback.

Catalog discovery is separate from activation. A catalogued package appears in the list even when disabled. `enabled.txt` explicitly activates source packages at startup. An enabled package can be discovered without a catalog line, but cataloguing disabled installed packages lets the list explain their status. There is no enable/disable management UI yet: edit activation files while closed and restart. Missing, incompatible and faulted packages show diagnostics; a package that cannot be discovered cannot display a page.

The default root is `mods` beside the executable. A bounded absolute `XCX_MOD_DIR` can select another source root; saved mod settings still live beside that executable in `mod-settings`, even when you choose another `--profile`. Different save profiles sharing one executable also share these mod settings. Keep executable folders/settings/logs and explicit profiles separate when testing multiple games.

## Settings and actions

Settings schema 1 supports bool, bounded integer and enum fields, at most eight fields and eight options per enum. Values are all-or-default validated at startup. Unsafe/rejected files are not silently overwritten. Stable mod/field/option IDs preserve preferences across compatible source updates.

Actions are optional declarations on the same page, at most four, with stable IDs and bounded labels. They are transient FIFO requests and never saved as toggles. `host.ui.status(text)` copies a bounded read-only line; `host.ui.enable(id, boolean)` enables your own button. A script-only action needs no provider. See the runnable example and [action contract](../provider-actions.md).

A page with only actions can declare `fields={}`. Provider-gated actions use `provider=true`, but need an explicitly trusted native association to become available. Use typed command IDs described by that provider; don't encode an address or structured message into an unrelated scalar.

## Find errors and logs

Open F1/Esc → Mods and select the package for its status and bounded diagnostic. `host.log` and protected-callback errors include the package ID and phase in the host log. For an ordinary launch, look beside the executable in `logs/xcx.log`; older runs rotate to `xcx.1.log` through `xcx.4.log`. The same messages go to terminal stderr. If your launcher redirects stderr to a regular file (for example `2> author-test.log`), inspect that file: this build does not also create its rotating log in that case. Keep logs separate for each test instance. Logging is limited to 16 script messages per state, so log transitions rather than every counter update.

## Update or disable

Close the entire game process before changing scripts, activation lists or native libraries. Closing only the overlay does not reload them. Replace only package files, preserve `mod-settings`, executable settings, profiles and logs, then restart. Removing `hello` from enabled.txt disables its Lua state; retaining the catalog entry keeps it listed as Disabled. Never replace a profile as part of installing a Lua mod. UI/events provide no game-save editing API.

If you disable this example, remove only its `hello` line from `enabled.txt`, keep it in `catalog.txt`, and restart. It should appear as Disabled. Its saved checkbox stays in `mod-settings/hello.ini`; enable it and restart to use that value again. Examples installed under `examples/` are not activated automatically.
