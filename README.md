# BBC

Bi-Bi-Coin is an educational C++ project for learning how a blockchain works.

## Prerequisites

- Visual Studio Code
- Microsoft C/C++ extension
- CMake Tools extension
- Visual Studio Build Tools with the MSVC compiler and Windows SDK
- CMake and Ninja
- Python 3.9 or newer

## Build and run

The cross-platform build helper prepares the native compiler environment,
configures the project, builds it, and runs the executable:

```console
python tools/build.py run
```

Available actions:

```console
python tools/build.py configure
python tools/build.py build
python tools/build.py run
python tools/build.py shell
```

On Windows, the helper automatically discovers Visual Studio Build Tools and
configures MSVC without requiring PowerShell execution-policy changes. On Linux
it uses GCC, and on macOS it uses Clang.

## Visual Studio Code

Open the repository in Visual Studio Code, then:

1. Run `CMake: Select Configure Preset` and select `Windows MSVC Debug`.
2. Run `CMake: Build`.
3. Run the program from the integrated terminal:

```powershell
.\build\windows-msvc-debug\bbc.exe
```
