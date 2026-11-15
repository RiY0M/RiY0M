A little app to compare big folders

The implementation is in C++ (`/`, C++20 + Qt 6).
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
