![CI](https://github.com/pbosetti/panelist/actions/workflows/ci.yml/badge.svg?branch=main)

# Panelist

Panelist is a header-only C++20 helper for splitting terminal output into
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
