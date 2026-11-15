A little app to compare big folders

The reference implementation is now in C++ (`cpp/`, C++20 + Qt 6). The original
Tkinter version is kept at the root as a specification and for the parity tests.
See `CLAUDE.md` for features, architecture, build and test instructions.

## Build (C++)

```bash
cd cpp
cmake --preset debug -DCMAKE_PREFIX_PATH=<Qt 6 install>   # e.g. C:/Qt/6.12.0/mingw_64 (MinGW: see CLAUDE.md)
cmake --build --preset debug
ctest --preset debug
```

Without Qt: `cmake --preset core && cmake --build --preset core && ctest --preset core`.

## Improvements to do
- add skip and delete buttons for file management
