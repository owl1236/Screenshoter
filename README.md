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
cl /std:c++17 /EHsc /O2 /W4 /FeScreenshoter.exe screenshoter.cpp stb_image_write.h /link /SUBSYSTEM:WINDOWS
```

This produces a single `Screenshoter.exe` in the current directory.

Flag notes:
- `/std:c++17` — `std::filesystem` requires C++17
- `/EHsc` — standard exception handling (used by `std::filesystem`)
- `/O2` — optimization
- `/SUBSYSTEM:WINDOWS` — GUI subsystem so the console window stays hidden
- Libraries (`gdi32`, `user32`, `shell32`, `ole32`, …) are linked automatically via `#pragma comment(lib, ...)` in the source
