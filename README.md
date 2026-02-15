# MyuEngine

Cross-platform 2D/3D app and game engine editor (SDL2 + OpenGL + ImGui).

## Install and Package

### Windows (installer .exe)

#### Option A: GitHub Actions (recommended)
1. Push the repo to GitHub.
2. Open the repo on GitHub and go to Actions.
3. Run the "Windows Installer" workflow.
4. Download the artifact named "MyuEngine-Installer".
5. Run the generated .exe on Windows.

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

The installer will be created in the build directory as MyuEngine-*.exe.

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

The package will be created in the build directory as MyuEngine-*.tar.gz.

### Linux (run from build)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/bin/MyuEngine
```

## Data Root and Portable Mode

The editor stores project data under the OS user data directory by default.
You can override the data root in the app Settings or by creating a file
named myuengine_home.txt next to the executable, containing the target path.

## License

See LICENSE.txt.
