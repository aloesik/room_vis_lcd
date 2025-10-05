### Brief

This repository is an ESP-IDF project (CMake-based) that builds firmware for an ESP32 series MCU and includes LVGL as a managed component. The goal of this file is to provide concise, actionable guidance for AI code assistants so they can be immediately helpful in this codebase.

### Big picture (what to know fast)
- Project root uses ESP-IDF + CMake. Key files: `CMakeLists.txt` (project entry), `sdkconfig` (build configuration), `main/CMakeLists.txt`, and `main/main.c` (app entry).
- UI code relies on LVGL located under `managed_components/lvgl__lvgl/` — treat it as a third-party component (do not edit upstream files unless contributing upstream changes).
- Build output is in `build/` and produces `room_vis_lcd.bin` and `room_vis_lcd.elf`.

### Typical developer workflows (explicit commands)
- Build (from project root): use ESP-IDF wrapper `idf.py` (requires ESP-IDF in PATH). Example:

```powershell
idf.py build
```

- Flash to device (assumes serial port set in environment or `sdkconfig`):

```powershell
idf.py -p COM3 flash
```

- Monitor serial output:

```powershell
idf.py monitor
```

- Quick build+flash+monitor:

```powershell
idf.py -p COM3 flash monitor
```

Note: project already has a `build/` directory with artifacts from prior builds. When modifying build logic, prefer `idf.py fullclean` or remove `build/` before rebuilding.

### Project-specific patterns & conventions
- Components: each component (including `main`) uses `idf_component_register()` in `CMakeLists.txt`. See `main/CMakeLists.txt` for example usage.
- Entry point: `app_main()` in `main/main.c` is the firmware entry. New tasks, timers, and LVGL initialization typically start here.
- LVGL usage: LVGL is used as a managed component under `managed_components/lvgl__lvgl/`. Look for configuration/porting glue in `components/esp-idf/` or `main/` if present.
- Avoid editing third-party managed component sources unless implementing a temporary patch; prefer adding wrapper code in `components/` or `main/`.

### Integration points & dependencies
- Hardware abstraction and drivers come from ESP-IDF (under `esp-idf/` in `build/` tree and system components). Check `sdkconfig` for enabled peripherals (SPI, I2C, LCD drivers).
- LVGL is integrated as a component; platform display/input bindings may be in `main` or `components/`.
- If adding new drivers, follow ESP-IDF component patterns: headers in `include/`, sources in `src/`, `CMakeLists.txt` with `idf_component_register()`.

### How to make safe edits
- Small changes: edit `main/main.c` or add a new component under `components/` and run `idf.py build`.
- Wiring new LVGL screens: add C files under `main/` or a component and call initialization from `app_main()`.
- When changing build flags or component lists, update `CMakeLists.txt` and run a clean build.

### Files to inspect for decisions/examples
- `main/main.c` — app entry and common patterns for task creation and peripheral init.
- `main/CMakeLists.txt` — how `main` is registered as a component.
- `managed_components/lvgl__lvgl/` — LVGL upstream source; read its `README.md` and `docs/` for API and porting notes.
- `sdkconfig` — which features and drivers are enabled.

### Quick tips for AI assistants
- When editing code, prefer small, reversible changes and include a short rationale in the commit message.
- For UI work, search for `lv_*` symbols (e.g., `lv_init`, `lv_task_handler`) to find where LVGL is initialized/used.
- To run or test code paths that depend on hardware, mock peripheral interfaces or isolate logic into testable functions.

If anything here is unclear or you'd like additional examples (e.g., how LVGL is initialized in this repo), tell me which area to expand and I will iterate.
