# Screenshoter

A small Windows tray application that captures a full-screen screenshot and saves it as a PNG.

- Global hotkey: **F6**
- Left-click the tray icon to open the menu (capture, open output folder, exit)
- Screenshots are saved next to the executable, named like `screenshot_2026-01-01_12-00-00.png`
- Single instance (a named mutex prevents duplicate runs)
- PNG encoding via the bundled `stb_image_write.h` (public domain)

## Building

Requirements: Visual Studio with the C++ toolset (`cl.exe`).

From the **Developer Command Prompt for VS** (so `cl` is on PATH):

```bat
cl /std:c++17 /EHsc screenshoter.cpp /link gdi32.lib user32.lib kernel32.lib shell32.lib ole32.lib /subsystem:windows /out:screenshoter.exe
```

This produces a single `screenshoter.exe` in the current directory.

Flag notes:
- `/std:c++17` — `std::filesystem` requires C++17
- `/EHsc` — standard exception handling (used by `std::filesystem`)
- `/subsystem:windows` — GUI subsystem so the console window stays hidden
- `/out:screenshoter.exe` — output filename
- `gdi32.lib user32.lib kernel32.lib shell32.lib ole32.lib` — Win32 libraries the app links against
- `stb_image_write.h` is **not** listed on the command line — it is pulled in by `#include "stb_image_write.h"` in the source. Passing a `.h` to `cl` treats it as a source file (warning D9024) and breaks the `W`-suffixed Win32 API calls.
