# xyz2las

A command line tool to convert XYZ text files to ASPRS LAS format using libLAS.

## Features

*   Converts ASCII XYZ data to binary LAS format.
*   Supports custom scale factors.
*   **Automatic Dependency Management**: Uses CMake's `FetchContent` to download and compile `libLAS`, `LASzip`, and `libgeotiff` automatically.
*   **Compressed Output**: Supports LASzip compression (laz) out of the box.
*   **Cross-Platform**: Works on Linux, Windows, and macOS.

## Requirements

*   **CMake**: Version 3.14 or later.
*   **C++ Compiler**: Must support C++11.
*   **Boost Libraries**: `libLAS` requires Boost headers.
    *   *Linux (Debian/Ubuntu)*: `sudo apt-get install libboost-all-dev`
    *   *macOS*: `brew install boost`
    *   *Windows*: Install Boost and set `BOOST_ROOT` environment variable.
*   **System Libraries**: `libLAS` and its dependencies require the following system libraries:
    *   **ZLIB**
    *   **GDAL**
    *   **PROJ**
    *   **TIFF**
    *   **JPEG**
    *   *Linux (Debian/Ubuntu)*: `sudo apt-get install zlib1g-dev libgdal-dev libproj-dev libtiff-dev libjpeg-dev`
    *   *macOS*: `brew install zlib gdal proj libtiff jpeg`

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
./xyz2las <input.xyz> <output.las|output.laz> [scale]
```

### Arguments

*   `input.xyz`: Input text file containing 3D coordinates. Format: `X Y Z` per line.
*   `output.las` / `output.laz`: Output file path. Use `.laz` extension to enable compression.
*   `scale`: Optional scale factor (default 0.01).
*   `scale`: (Optional) Scale factor for storing coordinates as integers. Default is `0.01` (preserves 2 decimal places). Use `0.001` for mm precision.
