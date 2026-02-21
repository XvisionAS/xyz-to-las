# xyz2las

A command line tool to convert XYZ text files to ASPRS LAS format using libLAS.

## Features

- **Extremely Fast**: Uses memory-mapped file I/O (`mio`) and highly optimized string-to-float parsing (`fast_float`) to process millions of points per second.
- **Real-time Progress**: Displays accurate progress bars based on file size during scanning and writing.
- **Colorization**: Supports colorizing points based on their Z-height (dark to light) using the `-c` or `--color` flag.
- **Automatic Dependency Management**: Uses CMake's `FetchContent` to download and compile `libLAS`, `LASzip`, and `libgeotiff` automatically.
- **Compressed Output**: Supports LASzip compression (laz) out of the box.
- **Cross-Platform**: Works on Linux, Windows, and macOS.
- **CI/CD**: Automated builds and releases via GitHub Actions.

## Requirements

- **CMake**: Version 3.14 or later.
- **C++ Compiler**: Must support C++11.
- **Boost Libraries**: `libLAS` requires Boost headers.
  - _Linux (Debian/Ubuntu)_: `sudo apt-get install libboost-all-dev`
  - _macOS_: `brew install boost`
  - _Windows_: Install Boost and set `BOOST_ROOT` environment variable.
- **System Libraries**: `libLAS` and its dependencies require the following system libraries:
  - **ZLIB**
  - **GDAL**
  - **PROJ**
  - **TIFF**
  - **JPEG**
  - _Linux (Debian/Ubuntu)_: `sudo apt-get install zlib1g-dev libgdal-dev libproj-dev libtiff-dev libjpeg-dev`
  - _macOS_: `brew install zlib gdal proj libtiff jpeg`

## Build

### Linux / macOS

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Windows

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Usage

```bash
./xyz2las <input.xyz> <output.las|output.laz> [scale] [-c|--color]
```

### Arguments

- `input.xyz`: Input text file containing 3D coordinates. Format: `X Y Z` per line.
- `output.las` / `output.laz`: Output file path. Use `.laz` extension to enable compression.
- `scale`: (Optional) Scale factor for storing coordinates as integers. Default is `0.01` (preserves 2 decimal places). Use `0.001` for mm precision.
- `-c` / `--color`: (Optional) Colorize points based on their Z-height (dark to light).
