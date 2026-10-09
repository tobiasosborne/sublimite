Implemented the always-lower-case amendment: UTF-8 title and `_NET_WM_NAME` are `sublimité`; ASCII `WM_NAME` and both `WM_CLASS` parts are `sublimite`.

Changed files:

- `src/x11/x11.c`, `src/editor/open.c`
- `tools/sublimite.desktop`, `tools/test_runtime_identity.sh`
- `tests/x11_identity_test.c`, `tests/editor_test.c`, `tests/cli_test.c`
- X11, editor and IPC `STATUS.md` files
- [rename-sublimite.md](/home/tobias/Projects/editor/.wt/edit-457.18/docs/decisions/rename-sublimite.md), including amendment evidence

CLI usage was already lower case. `src/main.c` is byte-for-byte unchanged, preserving `EDIT_ALLOW_REAL_DISPLAY` handling and `EDIT_TRACE_DUMP`. No font work.

Tests changed first. Red X11 run, (M)[AC], BAT0=Not charging, load1=7.28:

```text
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:19: FAIL memcmp(xcb_get_property_value(r), want, len) == 0
x11_identity_test:31: FAIL failed == 0
RED x11_identity_test exit=1
```

Red desktop run, (M)[AC], load1=6.66:

```text
+ grep -q ^Name=sublimité$ tools/sublimite.desktop
RED test_runtime_identity exit=1
```

Focused green, (M)[AC], load1=9.66:

```text
x11_identity_test: WM_CLASS, UTF8_STRING _NET_WM_NAME and WM_NAME readback passed on :99
cli_test: file window identity, XDG session journals, quiescent trace dump and dump error passed
editor_test: all passed
test_runtime_identity: binary, usage and desktop passed
test_runtime_identity: isolated XDG desktop installation passed
focused green exit=0
```

Required targets passed; each stamp is (M)[AC], BAT0=Not charging:

```text
load1=9.20:  make all exit=0
load1=10.77: check: 37 test binaries passed
            test_replay_cli: all passed
            make check exit=0
load1=10.40: fuzz: 20 fuzzers built
            make fuzz exit=0
```

X11 input fuzz smoke, (M)[AC], load1=12.27, shared-box TRACK:

```text
Done 226513 runs in 11 second(s)
x11_input_fuzz exit=0
```

Bench lines: none; identity strings only. All live runs used `DISPLAY=:99 EDIT_DISPLAY=:99`; the real-display opt-in was unset.

Open problems: no new failures. LeakSanitizer remains for the coordinator’s rerun; sanitizer checks used `detect_leaks=0`.