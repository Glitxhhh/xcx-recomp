# 03 · Menu registration v1

A loaded Lua mod can request one native child row in the game's existing Online submenu. Selecting it opens its existing F1/Esc → Mods page. The original five Online descriptors/actions and their service/progression checks are preserved; the Online parent is not replaced.

The current adapter supports only XCX DE 2.0.0 with checked generation enabled. Capacity is **one additional row total**, shared by all packages. This is not arbitrary menu injection. Every other mod remains usable through F1/Esc → Mods.

## Register after declaring your page

Call during `init`, after `host.settings.register` creates the named page:

```lua
local ok,reason=host.menu.register {
 version=1,
 location='online',
 id='workshop',
 label='Workshop',
 page='preferences'
}
if not ok then host.log('Online shortcut unavailable: '..reason) end
```

Add this to the first-mod init function if you want the shortcut. The Co-op package owns an equivalent declaration with label **Co-op** and page **loopback**, conditioned on its compatible provider. Recomp contains no permanent Co-op caption, weak symbol or networking policy.

A successful call returns true. Expected rejection returns `nil, reason`:

| Reason | Meaning |
| --- | --- |
| unsupported | Unsupported API version/location or executable without the checked adapter |
| full | Another registration already reserved the single native slot |
| unavailable | Wrong registration phase, missing target page or failed renderer availability |

Malformed strings/IDs raise a protected Lua error and fault the owner. IDs use lowercase ASCII letters first, then letters/digits/dot/underscore/hyphen, up to 64 bytes. The label is 1–32 characters, limited to ASCII letters, digits, spaces, dot, underscore, hyphen and parentheses. Markup, control characters and unverified Unicode/font behavior are excluded. Labels are copied into owned storage, not borrowed from Lua.

Only init can register, once per process slot. The first successful initialization wins; duplicates/conflicts return full. There is no runtime unregister, rename or reassignment. If an owner faults after reserving the slot, other mods still use F1/Esc Mods; the slot is not silently transferred while a controller may refer to it. Conditions are mod-owned startup decisions: skip registration when your required service is unavailable. Session actions stay on the registered page.

## Lifetime and return behavior

The checked adapter installs immutable runtime copies of the original descriptors plus one owned row in newly constructed controllers. Caption and descriptor storage remain valid until process exit, including after an owner fault. No original table is modified and no borrowed controller/view pointer is retained by the registry.

Selecting the owned row queues an owner/entry/page request to the existing host UI. Lua never runs in the game adapter or renderer. The game selection remains at the child level; Back/Continue closes the same overlay and returns to the submenu without reconstructing it. Existing input capture suppresses held opening input and waits for release on close. F1/Esc retains its general Mods/settings behavior.

A missing/faulted owner cancels opening; an already-installed row consumes selection safely instead of falling into an original action. It may remain visible for that controller lifetime. Future controllers do not install a faulted owner's extension. Construction/destruction conservatively invalidates the one global request epoch; another controller can therefore close an entry-opened page. Text entry has priority, renderer loss cancels pending requests, and shutdown prevents later UI entry. These bounded lifetime rules are intentional; independent concurrent controller pages are unavailable.
