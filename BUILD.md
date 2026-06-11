# Building pi-borescope-streamer

This project utilizes modern CMake with `CMakePresets.json` to guarantee reproducible builds across local development environments (macOS) and target hardware (Raspberry Pi). 

## Command Line Workflow (macOS & Raspberry Pi)

Because the project relies on Presets, the build commands are identical regardless of your operating system.

**Standard Release Build (Fast & Lean)**
*Compiles only the core application binaries. Testing and static analysis dependencies are ignored to speed up the build.*
```bash
# Configure the build tree once
cmake --preset release

# Compile the binaries
cmake --build --preset release -j$(nproc)

# Run as usual
./out/build/release/mjpeg_server

```

*(Note: If pulling new CMake changes to a clean board for the first time, run the configure step with `cmake --preset release --fresh` to clear out any old cached variables).*

**Running Tests**

```bash
cmake --preset test
cmake --build --preset test -j$(nproc)
cd out/build/test && ctest --output-on-failure

```

**Running Static Analysis (Clang-Tidy, Cppcheck, IWYU)**

```bash
# Configure for full static analysis
cmake --preset analysis

# Build with Clang-Tidy and IWYU natively enabled
cmake --build --preset analysis -j$(nproc)

# Generate a standalone Cppcheck XML report
cmake --build --preset cppcheck

```

**Running Benchmarks**

```bash
cmake --preset benchmark
cmake --build --preset benchmark -j$(nproc)

```

---

## Visual Studio Code Setup (Recommended)

Out of the box, VS Code's default C++ tools conflict with `clangd`, leading to broken IntelliSense and red squiggles. Follow this setup to get a fully integrated, JetBrains-style development and debugging experience.

### 1. Required Extensions

Ensure the following extensions are installed:

* **clangd** (`llvm-vs-code-extensions.vscode-clangd`): For superior code intelligence and formatting.
* **CMake Tools** (`ms-vscode.cmake-tools`): For natively reading the `CMakePresets.json` file.
* **CodeLLDB** (`vadimcn.vscode-lldb`): A fast, reliable debugger for LLVM/macOS environments.
* **C/C++** (`ms-vscode.cpptools`): Microsoft's default extension (we will disable its language server in the next step, but it is sometimes useful to keep around as a fallback).

### 2. Resolving IntelliSense Conflicts (`.vscode/settings.json`)

To stop the Microsoft C/C++ extension from fighting `clangd`, you must explicitly disable its IntelliSense engine and tell `clangd` where CMake is putting your compile commands.

Create or update `.vscode/settings.json` in the root of the project:

```json
{
    "cmake.copyCompileCommands": "${workspaceFolder}/compile_commands.json",
    "clangd.arguments": [
        "--compile-commands-dir=${workspaceFolder}/out/build/debug",
        "--header-insertion=iwyu",
        "--background-index"
    ],
    "C_Cpp.intelliSenseEngine": "disabled",
    "C_Cpp.autocomplete": "disabled",
    "C_Cpp.errorSquiggles": "disabled"
}

```

### 3. Debugging Tests (`.vscode/launch.json`)

To enable one-click debugging that maps directly to the active CMake target, configure CodeLLDB.

Create or update `.vscode/launch.json` in the root of the project:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug CTest Target",
            "type": "lldb",
            "request": "launch",
            "program": "${command:cmake.launchTargetPath}",
            "args": [],
            "cwd": "${command:cmake.launchTargetDirectory}"
        }
    ]
}

```

### 4. The "Anti-UI" Development Loop

The visual UI provided by CMake Tools can sometimes lose track of test binaries, especially when adding new source files. To guarantee a reliable build-and-debug cycle, rely on the Command Palette instead of the sidebar icons:

1. **Set the Environment:** Open the Command Palette (`Cmd + Shift + P`) and lock in the test preset:
* `CMake: Select Configure Preset` -> **test**
* `CMake: Select Build Preset` -> **test**
* `CMake: Set Build Target` -> **run_project_tests**


2. **Build Explicitly:** Make your code changes, then press `Cmd + Shift + B` (or `Ctrl + Shift + B` on Linux) to incrementally compile the test binary.
3. **Debug:** Drop a breakpoint in your `.cpp` file and press `F5`. CodeLLDB will attach directly to the newly built binary.

