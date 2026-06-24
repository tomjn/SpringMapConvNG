# SpringMapConvNG

SpringMapConvNG is a map compiler for the spring/recoil rts engine.

It also contains a decompiler, `mapdecompile`.

For details see https://springrts.com/wiki/MapConvNG

## Build

You need a C++11 compiler, CMake (>= 3.10) and DevIL.

Install DevIL:

- **Linux (Debian/Ubuntu):** `sudo apt-get install libdevil-dev`
- **macOS (Homebrew):** `brew install devil`
- **Windows (vcpkg):** `vcpkg install devil:x64-windows`

Then configure and build out of source:

    cmake -S . -B build
    cmake --build build

On Windows pass the vcpkg toolchain to the configure step:

    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_INSTALLATION_ROOT%/scripts/buildsystems/vcpkg.cmake

The `mapcompile` and `mapdecompile` binaries are written to `build/`.

## Releases

Pushing a tag of the form `1.1.1` (no leading `v`) builds binaries for Linux,
macOS and Windows and publishes them as a GitHub release. See
`.github/workflows/release.yml`.
