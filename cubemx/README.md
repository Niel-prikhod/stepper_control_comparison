# CubeMX projects

One CubeMX project per board, kept under `cubemx/<BOARD>/`:

- `cubemx/G491RE/G491RE.ioc` -> `cubemx/G491RE/`
- add future boards as `cubemx/<BOARD>/`

CubeMX owns everything in these folders. Do not move the project, do not edit
generated files by hand, and do not use the `CMakeLists.txt`,
`CMakePresets.json`, or `cmake/` that CubeMX emits. This repo builds the same
sources through `src/platform/CMakeLists.txt` and the board files in
`cmake/boards/`.

CubeMX may generate either `Core/Inc` + `Core/Src` or `Inc` + `Src`; the
platform CMake detects both. If the board BSP is enabled (e.g. Nucleo LEDs,
button, VCP), set `BOARD_BSP_DEFINE` in `cmake/boards/<board>.cmake` to the
matching CubeMX symbol (`USE_NUCLEO_64` for Nucleo-64).

## Recommended CubeMX settings

- Toolchain: CMake
- `ProjectManager.NoMain=true` (app/main.c owns `main()`)
- Keep user code enabled
- Either:
  - "Copy only the necessary library files" -> HAL lands in
    `cubemx/<BOARD>/Drivers/` and the repo is self-contained, or
  - "Do not copy library files" -> HAL is read from the STM32Cube repository
    via `CUBE_FW_PATH` / `STM32CUBE_FW_G4` (see top-level README).

> [!WARNING]
> Never edit files under `cubemx/<BOARD>/` by hand! 
> Change the `.ioc` instead.
