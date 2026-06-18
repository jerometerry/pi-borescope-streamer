# Building pi-borescope-streamer

This project utilizes modern CMake with `CMakePresets.json` to guarantee reproducible builds across local development environments (macOS) and target hardware (Raspberry Pi).

## Dependencies

### Core Build Tools & Compilers

These are required just to configure the project and compile the standard release binaries.

- **macOS (Homebrew):**

```bash
xcode-select --install # Provides AppleClang, Git, and base system headers
brew install cmake ninja pkg-config

```

- **Raspberry Pi / Debian (APT):**

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config git

```

### Required System Libraries

Your project natively links against OpenSSL, Zlib, and Libusb. These must be present on the system before CMake can successfully generate the build tree.

- **macOS (Homebrew):**
- _(Note: macOS ships with Zlib, so you only need to install OpenSSL and Libusb)._

```bash
brew install openssl@3 libusb

```

- **Raspberry Pi / Debian (APT):**

```bash
sudo apt install libssl-dev zlib1g-dev libusb-1.0-0-dev

```

### Static Analysis Tools (The `analysis` Preset)

If a developer wants to run your comprehensive code-quality checks, they will need this stack.

- **macOS (Homebrew):**

```bash
brew install llvm cppcheck include-what-you-use

```

- _Note: Homebrew's `llvm` provides `clang-tidy`._

- **Raspberry Pi / Debian (APT):**

```bash
sudo apt install clang-tidy cppcheck iwyu

```

- **Python Requirement (Both Platforms):**
  To convert the Cppcheck XML output into the HTML report you specified in your README, the user must install the Pygments library via Python.

```bash
pip3 install --user pygments --break-system-packages

```

### Documentation Generation (The `docs` Target)

Doxygen requires the `dot` tool to generate dependency graphs, class diagrams, and include hierarchies. `dot` is packaged inside the Graphviz suite.

- **macOS (Homebrew):**

```bash
brew install doxygen graphviz

```

- **Raspberry Pi / Debian (APT):**

```bash
sudo apt install doxygen graphviz

```

### Hardware & Daemon Dependencies (Linux Only)

If a user is deploying this on a Raspberry Pi and wants to utilize the `v4l2-borescope-daemon` features to pipe the stream into `/dev/video*`, they need the Video4Linux loopback driver.

- **Raspberry Pi / Debian (APT):**

```bash
sudo apt install v4l2loopback-dkms v4l2-utils

```

_(FFmpeg is also highly recommended for testing the daemon, via `sudo apt install ffmpeg`)._

## Command Line Workflow (macOS & Raspberry Pi)

Because the project relies on Presets, the build commands are identical regardless of your operating system.

**Standard Release Build (Fast & Lean)**
_Compiles only the core application binaries. Testing and static analysis dependencies are ignored to speed up the build._

```bash
# Configure the build tree once
cmake --preset release

# Compile the binaries
cmake --build --preset release -j$(nproc)

# Run as usual
./out/build/release/v4l2_mjpeg_server

```

_(Note: If pulling new CMake changes to a clean board for the first time, run the configure step with `cmake --preset release --fresh` to clear out any old cached variables)._

**Running Tests**

```bash
cmake --preset test
cmake --build --preset test -j$(nproc)
ctest --test-dir out/build/test --output-on-failure

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

- **clangd** (`llvm-vs-code-extensions.vscode-clangd`): For superior code intelligence and formatting.
- **CMake Tools** (`ms-vscode.cmake-tools`): For natively reading the `CMakePresets.json` file.
- **CodeLLDB** (`vadimcn.vscode-lldb`): A fast, reliable debugger for LLVM/macOS environments.
- **C/C++** (`ms-vscode.cpptools`): Microsoft's default extension (we will disable its language server in the next step, but it is sometimes useful to keep around as a fallback).

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

- `CMake: Select Configure Preset` -> **test**
- `CMake: Select Build Preset` -> **test**
- `CMake: Set Build Target` -> **run_project_tests**

2. **Build Explicitly:** Make your code changes, then press `Cmd + Shift + B` (or `Ctrl + Shift + B` on Linux) to incrementally compile the test binary.
3. **Debug:** Drop a breakpoint in your `.cpp` file and press `F5`. CodeLLDB will attach directly to the newly built binary.
