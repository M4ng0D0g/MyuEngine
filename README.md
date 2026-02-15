# MyuEngine

Cross-platform 2D/3D app and game engine editor (SDL2 + OpenGL + ImGui).

## Features

### Project Management
- Create projects from templates: 2D Game, 3D Game, Web Frontend/Backend/Full-Stack, Desktop App, Library, Empty  
- Select project language: C++ (Native), Web (HTML/JS), Java, Python  
- Project metadata stored in `project.mye`

### UI Designer
- Drag-and-drop UI elements: Container, Text, Image, Button, Input, Checkbox, Toggle, Slider, Progress, Dropdown  
- Visual canvas with zoom, pan, grid  
- Edit properties: position, size, color, font, border  
- Export/import HTML+CSS with bidirectional editing  
- Delete key to remove selected elements

### Game Editor
- Scene hierarchy, Inspector panel, Game Viewport  
- ECS (Entity-Component-System) with name and tag components  
- Resource manager for textures, models, materials, audio, fonts  
- Event system with emit/dispatch/subscribe

### Flow Chart Designer
- Visual node-based flow chart: Start, End, Process, Decision, Class, Function, I/O, Loop  
- Drag nodes, connect with bezier curves, right-click context menu  
- Class nodes support members, methods, inheritance  
- Function nodes support return type, parameters  
- **Generate code** in C++, Java, Python, or JavaScript based on project language  
- Save/load flow charts as `.myuflow` files (auto-loaded on project open)

### Build & Run
- Build menu with Build (Ctrl+B), Run (Ctrl+R), Build & Run (Ctrl+Shift+B)  
- Language-aware build commands:
  - C++: `cmake -B build && cmake --build build`
  - Java: `javac -d build`
  - Python: `python -m py_compile`
  - Web: no build step needed  
- Output streams to Log Console with error/warning highlighting  
- Stop button to terminate running processes

### Tools
- **Voxel Modeler**: slice-based voxel editing  
- **Blockbench Import**: read `.bbmodel` model metadata  
- **Resources**: manage project assets (textures, models, materials, audio, fonts)  
- **ECS**: create and inspect entities with components  
- **Events**: emit, dispatch, and monitor events

### Localization
- Traditional Chinese (繁體中文) and English  
- Translation files in `lang/` directory (`.lang` format)  
- Add new languages by creating `lang/<locale>.lang`  
- Format: `key=translated text` (one per line, `#` for comments)

### Editor
- ImGui docking layout with default panel arrangement  
- Multi-viewport support (separate OS windows)  
- Performance settings: VSync, FPS limit, idle FPS, pause when unfocused/minimized  
- Dark theme  
- Reset layout from Settings menu  
- In-app Usage Guide panel

## Install and Package

### Windows (installer .exe)

#### Option A: GitHub Actions (recommended)
1. Push the repo to GitHub.
2. Open the repo on GitHub and go to Actions.
3. Run the "Windows Installer" workflow.
4. Download the artifact named "MyuEngine-Installer".
5. Run the generated .exe on Windows.

The installer supports Repair / Reinstall / Uninstall when a previous installation is detected.

#### Option B: Local Windows build + NSIS
Prerequisites:
- Visual Studio 2022 (C++ toolchain)
- CMake 3.20+
- NSIS installed and available in PATH

Commands:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build
cpack -G NSIS -C Release
```

### Linux (TGZ package)

Prerequisites:
- CMake 3.20+
- GCC 10+ (or Clang)
- SDL2 build dependencies (X11/GL dev packages)

Commands:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build
cpack -G TGZ -C Release
```

### Linux (run from build)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bin/MyuEngine
```

## Project Structure

```
MyuEngine/
├── src/
│   ├── main.cpp              # Editor entry point
│   ├── ui_designer/          # UI Designer (UIElement, UIDesigner, UIHtmlCss)
│   ├── editor/               # Game Editor, Voxel Editor
│   ├── engine/               # ECS, Resources, EventBus
│   └── tools/                # Blockbench Import
├── lang/
│   ├── zh_TW.lang            # Traditional Chinese
│   └── en_US.lang            # English (template)
├── external/                 # Third-party dependencies
├── extras/                   # Tools and samples
├── .github/workflows/        # CI/CD
├── CMakeLists.txt
└── README.md
```

## Data Root and Portable Mode

The editor stores project data under the OS user data directory by default.
You can override the data root in the app Settings or by creating a file
named `myuengine_home.txt` next to the executable, containing the target path.

## License

See LICENSE.txt.
