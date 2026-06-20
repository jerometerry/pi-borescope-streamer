# Building pi-borescope-streamer

This project utilizes **GNU Make** combined with the **Zig** compiler (`zig cc` / `zig c++`).

Switching to Zig provides a hermetic, drop-in C++23 compiler that guarantees reproducible builds, trivial cross-compilation, and strict Undefined Behavior Sanitization (UBSan) out of the box across both local development environments (macOS) and target hardware (Raspberry Pi).

_(Note: While CMake is no longer the build system, a minimal CMake installation is still required as a standalone script-runner to convert the HTML dashboard into a C++ header)._

## Dependencies

### Core Build Tools & Libraries

These are required to configure the project, download third-party dependencies, and compile the standard release binaries.

- **macOS (Homebrew):**

```bash
xcode-select --install # Provides Git and base system tools
brew install make cmake pkg-config libusb openssl@3 zig

```

- **Raspberry Pi / Debian (Linux):**

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config git libusb-1.0-0-dev libssl-dev zlib1g-dev

```

_(Note: You will need to install Zig manually from the [official Zig website](https://ziglang.org/download/) or via a package manager, as Debian repositories often contain heavily outdated versions)._

### Formatting and IDE Dependencies

If a developer wants to run the code formatting scripts or set up advanced VS Code IntelliSense, they will need the LLVM toolchain (for `clang-format` and `clangd`) and `bear` (to generate compilation databases from Makefiles).

- **macOS (Homebrew):**

```bash
brew install llvm bear

```

- **Raspberry Pi / Debian (Linux):**

```bash
sudo apt install clang-format clangd bear

```

### Hardware & Daemon Dependencies (Linux Only)

If a user is deploying this on a Raspberry Pi and wants to utilize the `v4l2-borescope-daemon` features to pipe the Presentation-Layer Video Frames into `/dev/video*`, they need the Video4Linux loopback driver.

```bash
sudo apt install v4l2loopback-dkms v4l2-utils

```

_(FFmpeg is also highly recommended for testing the daemon, via `sudo apt install ffmpeg`)._

---

## Command Line Workflow

Because the `Makefile` automatically detects the host OS (macOS vs. Linux), the build commands are identical regardless of your operating system. Third-party dependencies (`uSockets`, `uWebSockets`, `googletest`) are automatically cloned and compiled into the `build/` directory during the first run.

**Standard Build**
_Compiles the core application binaries. Testing libraries are ignored to speed up the build._

```bash
# Compile the project using Zig
make

# Run the appropriate binary (macOS example)
./build/mjpeg_server

```

**Running Tests**
_Compiles GoogleTest using Zig, links the test suites, and immediately executes them._

```bash
make test

```

**Code Formatting**
_Runs the `run-format.sh` script to align all source files with `.clang-format`._

```bash
# Automatically format all C++ files
make format

# Dry-run check (fails if formatting is required)
make check-format

```

**Clean the Build Tree**

```bash
make clean

```

---

## Visual Studio Code Setup (Recommended)

Out of the box, VS Code's default C++ tools conflict with `clangd`, leading to broken IntelliSense. Furthermore, raw Makefiles do not automatically tell VS Code where your header files are. Follow this setup to get a fully integrated development and debugging experience.

### 1. Required Extensions

Ensure the following extensions are installed:

- **clangd** (`llvm-vs-code-extensions.vscode-clangd`): For superior code intelligence.
- **CodeLLDB** (`vadimcn.vscode-lldb`): A fast, reliable debugger for LLVM/Zig environments.
- **C/C++** (`ms-vscode.cpptools`): Microsoft's default extension (we will disable its language server in the next step, but the debugger backend is occasionally useful).

### 2. Generating `compile_commands.json` (The Secret Sauce)

For `clangd` to understand your `#include` paths (like `uWebSockets` or `gtest`), it needs a compilation database. You generate this by running `make` through `bear`.

Run this command in your terminal whenever you add new files or change include paths:

```bash
make clean
bear -- make test

```

This will generate a `compile_commands.json` file in the root of your project, which `clangd` will automatically ingest.

### 3. Resolving IntelliSense Conflicts (`.vscode/settings.json`)

To stop the Microsoft C/C++ extension from fighting `clangd`, explicitly disable its IntelliSense engine.

Create or update `.vscode/settings.json` in the root of the project:

```json
{
  "clangd.arguments": ["--background-index", "--compile-commands-dir=${workspaceFolder}"],
  "C_Cpp.intelliSenseEngine": "disabled",
  "C_Cpp.autocomplete": "disabled",
  "C_Cpp.errorSquiggles": "disabled"
}
```

### 4. Debugging Tests (`.vscode/launch.json`)

To enable one-click debugging, configure CodeLLDB to attach directly to your compiled test binaries.

Create or update `.vscode/launch.json` in the root of the project:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug Project Tests",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/build/run_project_tests",
      "args": [],
      "cwd": "${workspaceFolder}"
    },
    {
      "name": "Debug Linux Driver Tests",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/build/run_linux_driver_tests",
      "args": [],
      "cwd": "${workspaceFolder}"
    },
    {
      "name": "Debug macOS Server",
      "type": "lldb",
      "request": "launch",
      "program": "${workspaceFolder}/build/mjpeg_server",
      "args": [],
      "cwd": "${workspaceFolder}"
    }
  ]
}
```

### 5. The Development Loop

Because we removed the heavy CMake Tools UI extension, your development cycle is now much faster and terminal-driven:

1. Write your code.
2. Run `make test` in the VS Code integrated terminal.
3. If a test fails, drop a breakpoint in the file, select **Debug Project Tests** in the VS Code Run & Debug sidebar, and press `F5`.
