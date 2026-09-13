# Vinci - Cross-Platform Windowing Library

Refer to `vinci.h` and `test.c` to get started with the API.

## Supported Platforms
- Linux   (via XCB)
- Windows (Win32)
- macOS   (Cocoa)
- Browser (WebAssembly + Canvas 2D)

## Status
- Unstable
- Undocumented
- API might change

## Building
Run `make` to build the test program.

For the browser demo, run `make web`, then serve `build/web` over HTTP:

```sh
python3 -m http.server 8000 --directory build/web
```

Open `http://localhost:8000`. Requires Clang with the WebAssembly target and LLD.
See [the browser backend guide](web/README.md) for the C/C++ build, host interface,
event conventions, lifecycle and browser tests. Desktop builds are unchanged.

## Legal
Copyright (C) 2021-2025 Orastron Srl unipersonale.

Authors: Paolo Marrone, Stefano D'Angelo.

All the code in the repo is released under GPLv3. See the LICENSE file.
