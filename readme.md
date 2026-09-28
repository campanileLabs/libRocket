# libRocket - The HTML/CSS User Interface Library

libRocket is a C++ user interface package based on the HTML and CSS standards. It is
designed as a complete solution for any project's interface needs.

libRocket uses the time-tested open standards XHTML1.0 and CSS2.0 (while borrowing features from
HTML5 and CSS3), and extends them with features suited towards real-time applications. Because of
this, you don't have to learn a whole new proprietary technology like other libraries in this space.

## Features

- Cross platform architecture: Windows, macOS, Linux, iOS, etc.
- Dynamic layout system.
- Efficient application-wide styling, with a custom-built templating engine.
- Fully featured control set: buttons, sliders, drop-downs, etc.
- Runtime visual debugging suite.
- Easily integrated and extensible with Python or Lua scripting.

## Extensible

- Abstracted interfaces for plugging in to any game engine.
- Decorator engine allowing custom application-specific effects that can be applied to any element.
- Generic event system that binds seamlessly into existing projects.

## Project Layout

- [Include/](Include) - Public headers for the Core, Controls, Debugger, and Lua/Python binding modules.
- [Source/](Source) - Library implementation source, mirroring the module layout under `Include/`.
- [Build/](Build) - CMake build scripts (`Build/CMakeLists.txt`) and platform helpers.
- [Dependencies/](Dependencies) - Location for third-party dependencies (FreeType, Lua, boost, SDL2, etc.) that are not bundled.
- [Samples/](Samples) - Example applications demonstrating library usage; see [Samples/README.TXT](Samples/README.TXT).
- [assets/](assets) - Shared runtime assets (fonts, styles) used by the samples and tutorials.
- [bin/](bin) - Default output directory for built libraries and sample binaries.

## Building

libRocket is built with [CMake](https://cmake.org/) (version 3.5+) from the [Build/](Build) directory.

### Prerequisites

- A C++ compiler with CMake support for your platform (MSVC, Clang, GCC, etc.)
- [FreeType](https://www.freetype.org/) (required)
- Optional, depending on which bindings/samples you enable:
  - [Lua](https://www.lua.org/) - for `BUILD_LUA_BINDINGS`
  - Python and Boost - for `BUILD_PYTHON_BINDINGS`
  - SDL2 / SDL2_image or SFML - used by some of the samples

Dependencies are looked for in [Dependencies/](Dependencies) by default; place extracted dependency packages there, or point CMake at their install locations (e.g. via `FREETYPE_DIR`, `LUA_DIR`, `BOOST_ROOT`, `SDL2DIR`, `SDL2_IMAGE_DIR`, `SFML_ROOT` environment variables).

### Configure and build (generic)

```bash
cd Build
cmake -B build .
cmake --build build
```

Useful CMake options (pass as `-DOPTION=ON`/`OFF`):

- `BUILD_SAMPLES` - build the sample applications (default `OFF`)
- `BUILD_LUA_BINDINGS` - build the Lua scripting bindings (default `OFF`)
- `BUILD_PYTHON_BINDINGS` - build the Python scripting bindings (default `OFF`)
- `ROCKET_STATIC_LIB` - build static libraries instead of shared libraries
- `BUILD_UNIVERSAL_BINARIES` (Apple only) - build universal/multi-architecture binaries

Example, building with samples and Lua bindings enabled:

```bash
cd Build
cmake -B build -DBUILD_SAMPLES=ON -DBUILD_LUA_BINDINGS=ON .
cmake --build build
```

### Building for MinGW (Windows)

See [how_to_build_for_mingw.txt](how_to_build_for_mingw.txt) for detailed steps on fetching FreeType and configuring CMake with the `MinGW Makefiles` generator.

### Samples

The [Samples/](Samples) directory contains standalone example applications (basic startup, Space Invaders clone in C++/Python/Lua, tutorials, etc.). Build them via CMake with `BUILD_SAMPLES=ON`, or via the platform-specific project/SCons files described in [Samples/README.TXT](Samples/README.TXT).

## License (MIT)

Copyright (c) 2008-2014 CodePoint Ltd, Shift Technology Ltd, and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
