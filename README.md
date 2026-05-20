[![CI](https://github.com/pbosetti/panelist/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/pbosetti/panelist/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://pbosetti.github.io/panelist/)

# Panelist

Panelist is a header-only C++17 helper for splitting terminal output into
vertically stacked panels. Panels can be used as scrolling logs or written by
line number, where line `0` is the bottom line of a panel.

The library does not depend on ncurses or other third-party terminal libraries.
When output is redirected, writes pass through normally.

## CMake

Panelist exports the `panelist::panelist` interface target.

### FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
  panelist
  GIT_REPOSITORY https://github.com/pbosetti/panelist.git
  GIT_TAG main)

FetchContent_MakeAvailable(panelist)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE panelist::panelist)
```

When Panelist is consumed as a subproject, examples and tests are disabled by
default. They are enabled by default only for a top-level checkout.

### Local Checkout

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

### Documentation

> Detailed class documentation is here: <https://pbosetti.github.io/panelist/>.

The `document` target is optional. Enable it when configuring, then build it:

```sh
cmake -S . -B build -G Ninja -DPANELIST_BUILD_DOCUMENTATION=ON
cmake --build build --target document
```

This requires Doxygen. The generated HTML documentation is written to
`build/docs/html/index.html`.

### Package

The `package` target creates a release tarball containing the public
`include` tree and generated Doxygen HTML documentation under `doc`:

```sh
cmake -S . -B build -G Ninja -DPANELIST_BUILD_PACKAGE=ON
cmake --build build --target package
```

The output file is written as `build/panelist-vX.Y.Z.tar.gz`, where `vX.Y.Z`
is the latest reachable git tag matching `vMAJOR.MINOR.PATCH`, or `v0.0.0`
when no matching tag is available.

## API

```c++
#include <panelist/panelist.hpp>

#include <iostream>

int main() {
  Panelist::Panelist panels(std::cout);
  panels.set_separator("=");
  panels.add_panel(2); // top panel, fixed height
  panels.add_panel();  // middle panel, flexible height
  panels.add_panel(2); // bottom panel, fixed height
  panels.layout();

  std::cout << panels[0][1] << "my_program" << std::endl;
  std::cout << panels[0][0] << "Short purpose statement" << std::endl;

  std::cout << panels[1] << "0, 42" << std::endl;
  std::cout << panels[1] << "1, 17" << std::endl;

  std::cout << panels[2][1] << "Running..." << std::endl;
  std::cout << panels[2][0] << "Lines appended: 2" << std::endl;

  panels.disable();
}
```

Use `panels[index]` to append to a panel as a scrolling log. Use
`panels[index][line_from_bottom]` to write a specific line. After `layout()`,
adding more panels is an error.

Useful methods:

- `set_separator(std::string)`: set the separator fill text
- `add_panel(height)`: add a fixed-height panel
- `add_panel()`: add the flexible panel
- `set_flexible_panel(index)`: choose which panel resizes with the terminal
- `clear(index)`: clear one panel
- `disable()` / `enable()`: temporarily leave and re-enter panel mode
- `reset()`: remove the layout so panels can be defined again
- `version()`: return the current version string, for example `v0.1.2`

The version string is taken from the latest reachable git tag matching
`vMAJOR.MINOR.PATCH`; if no matching tag is available, it defaults to
`v0.0.0`.

## Example

The three-panel example logs a random value once per second in the flexible
middle panel:

```sh
./build/panelist_three_panels
```

Pass a sample count as the first argument, or `0` to run until interrupted:

```sh
./build/panelist_three_panels 0
```

You will get something like:

[![asciicast](https://asciinema.org/a/wmkjWdV1fXIQsKLZ.svg)](https://asciinema.org/a/wmkjWdV1fXIQsKLZ)

# Author

Paolo Bosetti, University of Trento
