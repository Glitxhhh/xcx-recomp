# Controls

Input comes from the first SDL3 gamepad, the keyboard and the mouse, merged into one Switch controller (`runtime/input_win.c`). Buttons are ORed; each stick takes the source with the larger deflection. Keys and mouse buttons count only while the game window is in the foreground. `XCX_INPUT=none` disables both (automated runs). `XCX_*` settings can also be set in `xcx.ini` next to `xcx.exe` (`input = none`, `motion = 0`, `rumble = 0`); the environment overrides the file. `XCX_RUMBLE_STRENGTH` is the rumble strength in percent (0 to 300, default 130; 0 is the same as `rumble = 0`). `XCX_RUMBLE_CURVE` is the exponent applied to the amplitude before the strength (0.2 to 1.5, default 0.6; lower makes weak pulses stronger, 1 is linear). `XCX_RUMBLE_TRIGGERS=1` also drives the impulse triggers of an Xbox pad.

`XCX_PAD_LAYOUT` (`pad_layout`, the *Confirmation/Cancel* row on the Controls tab, live; options Automatic, Default and Flipped) picks which face button is Switch A:

- `nintendo` (shown as *Default*): by position, as on a Switch, the original layout (the table below).
- `xbox` (shown as *Flipped*, which swaps the confirmation and cancel buttons): south A, east B, west X, north Y, as an Xbox pad labels them; on a PlayStation pad cross is A, circle B, square X, triangle Y.
- `auto` (default): `nintendo` for a pad whose east button SDL reports labelled A (Switch Pro Controller, Nintendo-layout pads), `xbox` for the others.

The log names the layout in effect (`input: gamepad layout ...`); the menu shows it under *Automatic*. The game's button prompts stay Switch letters by default. The experimental HUD style preference below is separate from this input layout.

| Switch | Gamepad (by position, Nintendo layout) | Keyboard and mouse | Action in the world |
|---|---|---|---|
| Left stick | left stick | W A S D | move |
| Right stick | right stick | mouse; arrow keys as a second source | camera |
| Y | west | left mouse button | attack |
| ZL | left trigger (over 50%) | right mouse button, Z | shield, lock on |
| ZR | right trigger (over 50%) | F, X | bow |
| X | north | Space | jump |
| B | south | Left Shift | run, cancel |
| A | east | E | interact, pick up |
| L | left shoulder | Q | abilities |
| R | right shoulder | R | throw weapon |
| Left stick click | left stick click | Left Ctrl | crouch |
| Right stick click | right stick click | C | first-person view |
| D-pad left, up, right, down | D-pad | 1 2 3 4 | quick-select |
| Plus | Start | Tab | menu, inventory |
| Minus | Back | M | map |

Esc is unbound in the game; it opens the settings menu and releases the mouse cursor (see Mouse camera). Z and X duplicate ZL and ZR so menu pages turn with two neighbouring keys; the menu tabs (L, R) are Q and R. The mouse wheel is unbound. The bindings are the tables of `input_map` in `runtime/input_map.c`: the keys (platform-neutral key ids) and one gamepad table per layout (gamepad inputs by position); `runtime/input_win.c` translates Win32 virtual keys and SDL buttons.

## Window keys

| Key | Gamepad | Action |
|---|---|---|
| F1 or Esc | Minus and Plus together | open or close the settings menu; the game gets no input while it is open |
| F10 | L3 + R3 (both stick clicks) | performance overlay: off, frame rate, full panel |
| F11 | | borderless fullscreen on or off, saved to `xcx.ini` |

The Minus+Plus combo never reaches the game. In the menu the arrow keys, the D-pad or the left stick move, Enter or A selects, Q and E or L and R switch tabs, Esc or B goes back (Switch button names, as in the table above).

## Remapping

The *Controls* tab of the settings menu lists, under the button layout, every Switch button and the eight stick directions the keys drive (movement and camera), each with two key cells and a controller cell. Select a cell, press A or Enter, then press the new key, mouse button or controller button; Esc or five seconds without a press cancel, and a controller button cancels a key cell. Delete, Backspace or X clears the selected cell. A key another action has moves to the new one (the description names the action it left); a controller button swaps with the one it replaces. A remapped controller button is physical: from then on the face buttons (A B X Y) stay where they are in both layouts, until the buttons are put back as the layout in use has them or *Reset all buttons* is chosen. F1, F8, F10, F11 and Esc cannot be bound (the menu, the flight recorder and the window keep them). Punctuation keys are named by their place on a US keyboard; the menu shows the character the keyboard layout in use puts there.

The tab writes `xcx.ini` lines, one per Switch button or stick direction: `bind_<button> = <entry>, <entry>, ...`. The button is `A B X Y L R ZL ZR PLUS MINUS LEFT UP RIGHT DOWN LCLICK RCLICK`, or a stick direction `LSTICK_LEFT LSTICK_UP LSTICK_RIGHT LSTICK_DOWN` (movement, W A S D by default) and `RSTICK_...` (camera, the arrow keys). An entry is `pad:<name>` or `key:<name>`:

- pad names: `south east west north l r zl zr start back lstick rstick dpad_left dpad_up dpad_right dpad_down` (`zl`, `zr` are the triggers)
- key names: `a` to `z`, `0` to `9`, `f2` to `f7`, `f9`, `f12`, `num0` to `num9`, `space tab enter backspace lshift rshift lctrl rctrl lalt capslock insert delete home end pageup pagedown up down left right`, `num_add num_sub num_mul num_div num_decimal`, `minus equals lbracket rbracket semicolon apostrophe comma period slash backslash grave`, `mouse_l mouse_r mouse_m mouse_x1 mouse_x2`
- `key:default` and `pad:default` keep that part's defaults (`pad:default` is each layout's own button).
- `none` leaves the button unbound.

A line replaces every default of that button, pad and keyboard, so list the keys you still want or use `key:default`. Pad names are physical positions, so a line means the same in both layouts and replaces the layout's own button for that Switch button. Stick directions take keys only: the controller's sticks and the mouse camera always work. Buttons without a line keep the table above. Names are case-insensitive. An invalid entry logs one `input: bind ...` line and is skipped; a line without a valid entry changes nothing. The `XCX_BIND_<button>` environment variables work like any other setting. A changed line applies at once (`runtime/input_win.c` builds the tables again at its next poll).

A pad that reports its face buttons swapped (A/B and X/Y, as some GuliKit models do) is fixed with:

```
bind_A = pad:south, key:E
bind_B = pad:east, key:lshift
bind_X = pad:west, key:space
bind_Y = pad:north, key:mouse_l
```

## Mouse camera

Mouse motion (raw input) becomes right-stick deflection. Each presented frame takes the counts moved since the last frame, divides by the frame time and sets the deflection to `counts per second * XCX_MOUSE_SENS / 3000` of full, held for that frame, so the turn rate does not depend on the frame rate. This is not mouselook: the game's stick curve and dead zone still apply, so very slow movements do nothing and a fast flick saturates. The mouse deflection competes with the gamepad and arrow keys by the larger-deflection rule.

`XCX_MOUSE_SENS` (0 to 10, default 1; higher turns faster, 0 turns the mouse camera and the cursor capture off). While the window is the foreground window the cursor is hidden and confined to the client area. It is released on focus loss, minimise, while F11 changes the window, during a size or move loop and while a text entry is open. Esc releases it by hand: the cursor shows, the mouse camera and mouse buttons stop, and the window can be moved, minimised or closed. A mouse click inside the client area captures it again; that click does not reach the game. Activating the window by a click on its title bar or border does not capture, so it can be dragged or closed right away; Alt+Tab or a click in the client area captures as before. `XCX_INPUT=none` disables the capture too.

The stick direction bits (`StickLLeft` to `StickRDown`) are derived from the sticks at 50% deflection. A gamepad plugged in later is picked up within one second; with none connected the log says `input: no gamepad`.

## Scripted input

`XCX_INPUT_SCRIPT=<file>` replays presses. It is ORed with real input and still applies under `XCX_INPUT=none`. One event per line, `#` starts a comment:

```
<frame> <button> [hold_frames]            # A B X Y L R ZL ZR PLUS MINUS LEFT UP RIGHT DOWN LCLICK RCLICK; hold defaults to 4
<frame> LSTICK|RSTICK <x> <y> [hold_frames]   # -32767..32767, up is +y
@<ms> <button> [hold_ms]                  # timed on the world clock
@<ms> LSTICK|RSTICK <x> <y> [hold_ms]
```

`<frame>` counts 60 Hz ticks of game time since the first frame (presented frames at 60 fps), so the title menus get the presses at the same game time at any frame limit. `@<ms>` counts milliseconds of the world clock: game time stepped since the end of the first save load (the player leaves the title screen's origin), so a timed event lands at the same game time at any frame rate and load length. A later load, such as fast travel, still shifts what follows. In a script with timed lines the untimed ones stop when the world clock starts, as the load ends at another tick in each run. Each event logs once (`input: script frame N tick T: A`). `tools/input/load_save.txt` loads the first save from the title screen.

`XCX_INPUT_RECORD=<file>` (`input_record` in `xcx.ini`, or Input recording in the menu) writes the merged input in the same format, one line per button press and per stick segment: events before the world clock starts by tick, the rest timed; a button or stick held when the world clock starts is cut there and goes on as a timed event. The file replays as a script, which makes it the repro to attach to a bug report.

## RenderDoc capture (blocked)

`XCX_RDOC=<frame>[,...]` (`runtime/rdoc_win.c`; `XCX_RDOC_DIR`, `XCX_RDOC_DLL`) loads RenderDoc before `vkCreateInstance`. RenderDoc hides `VK_EXT_external_memory_host`, which the back end requires for guest pool imports (`runtime/vk_memory.c`), so device creation fails under it. A capture needs a copy-based pool fallback first.

## Experimental Appendage glyphs

In F1/Esc → Controls, **Button Icon Type** (Automatic, Nintendo Switch, PlayStation, Xbox, Steam Controller, Keyboard/Mouse; Automatic is the default and stores `auto`, Nintendo Switch stores `original`) changes selected button guides in verified XCX 2.0.0. Appendage, weapon-change X and NPC interaction prompts have passed Linux owner acceptance. Other shared field A-button captions, Target R and Board ZL are also adapted, but their visible acceptance remains pending. It offers Original (unchanged), Automatic, PlayStation, Xbox, Steam Controller and Keyboard / Mouse. Other HUD, menu and tutorial prompts remain unchanged; see the [coverage inventory](prompt-coverage.md). The guide still appears briefly on targeting/retargeting and hides normally.

Automatic follows fresh intentional keyboard/mouse or controller activity even with a controller connected. Held input and small stick drift do not take the display back. Mouse clicks count; sustained captured camera motion can count, while incidental movement, capture/focus motion and single spikes are filtered. These filters do not change gameplay dead zones or emitted inputs. Manual selection fixes artwork without changing bindings or the separate Button layout preference.

The guide represents virtual R held with Y or A for previous/next appendage. With the default Xbox input layout on a PlayStation pad, it shows R1 with Triangle / Cross; keyboard/mouse defaults show R with left mouse / E. It uses the current bindings, not fixed artwork-to-physical-button assumptions. Keyboard letter/digit glyphs follow the current unshifted keyboard labels.

Coverage is deliberately bounded: standard face/shoulder/trigger buttons for the named controller families; letters, digits, Space and left/right mouse for keyboard/mouse. Multiple, shared, unbound or unsupported bindings use the original virtual Switch labels for the whole chord, with a reason under the preference. Unknown controller families also use original labels. Steam Input virtual devices are identified only by the controller interface exposed to SDL; an exposed Xbox controller is treated as Xbox. Manual Steam Controller styling supports its exposed standard controls, not arbitrary paddles, touchpad regions or hidden physical hardware.

Original leaves game artwork unchanged. Automatic also keeps native virtual labels for intentional input from an exposed Nintendo controller. There is no separate Nintendo Switch style option. Native labels do not change to describe custom bindings. No Nintendo artwork is downloaded or installed.

**Button Appearance: Colored (default) / Standard** is separate from style and bindings. Colored face buttons uses licensed PlayStation, Xbox and Steam face-button variants. Other buttons, keyboard/mouse and native Nintendo retain Standard. Automatic honors the applicable appearance choice.

The existing **Confirmation/Cancel** option changes physical mappings: which physical button emits A/B/X/Y. Glyph style and appearance only change displayed artwork. They do not replace that option or alter your rebinding settings.

The weapon guide uses the current X binding (Space on the default keyboard map); field A, Target and Board guides use A, R and ZL respectively. The default keyboard ZL has multiple bindings, so Board retains native virtual artwork. Unsupported bindings keep the original virtual label for that guide.

The experimental renderer keeps a process-wide history of 192 successful button-node attachments shared between the Appendage and all registered shared-widget instances. Reusing a live shared guide, including a retained A/R/ZL selector change, does not consume it; reconstruction does, even while Original is selected. Once the budget is full, rebuilt guides keep original labels, and Controls explains that restarting the game restores styles. Changing styles does not reset the budget.

The preference persists as `glyph_style = auto|original|playstation|xbox|steam|keyboard` in `xcx.ini` (default `original`: the other styles cost frame rate for now, #136), alongside the separate `glyph_appearance = colored|standard` (default `colored`), independently of `pad_layout` and `bind_*`. Original is the default. It is unavailable for unverified game builds. This is experimental coverage, not universal prompt replacement or a claim of Windows/other-controller runtime acceptance.
