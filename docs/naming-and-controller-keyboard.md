# Character name entry and controller keyboard

XCX uses the larger SDK software-keyboard argument layout despite its zero build marker. The decoder and matching service probe use the verified work-buffer offset; no save edit or hardcoded name is involved.

The SDL editor supports physical text input and a controller grid of Latin letters, digits and Space. A selects a key; Plus or OK confirms; B or Cancel cancels; X/Y delete backward/forward; ZR holds Shift; ZL toggles Caps Lock; L/R move the text cursor. D-pad/left stick navigate. Configured virtual actions determine the physical mapping. Minimum/maximum length and disabled-character validation still apply. Focus/reconnect/opening presses require release before accepting input. No prompt-style preference or glyph artwork is included.

The source was reconstructed from the frozen November 6 naming/controller-keyboard snapshot on current upstream master. The historical snapshot and acceptance report remain preserved outside this branch. Production source and the C++ test are byte-identical to that snapshot; the Python test runner is also unchanged at this prerequisite stage.

Linux evidence: 55 headless checks with real ImGui text frames and mocked controller/focus samples; actual-game PlayStation naming and same-profile reload were owner-confirmed on accepted executable SHA-256 `038457447a9c9660603cf0f81ea9e8b2da27b96142c800b3e3eb49127b2f954a`. This does not establish native Windows, Steam Controller or Switch acceptance. Reconstruction does not guarantee byte-identical binaries. No guest-code regeneration is needed for this feature.

Run `python tests/controller_keyboard_check.py --build <matching-native-build>`, then the printed private executable with `--probe-window`. It is a headless test, not a gameplay launch. Build dependencies/toolchain are described in [building](building.md).
