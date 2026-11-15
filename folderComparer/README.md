A little app to compare big folders (C++20 + Qt 6).

Compare two folders, see what differs (files and whole missing folders), copy or
delete each difference, and undo any action. See `CLAUDE.md` for features,
architecture, build and test instructions.

## Build

```bash
cmake --preset release -DCMAKE_PREFIX_PATH=<Qt 6 install>   # e.g. C:/Qt/6.12.0/mingw_64 (MinGW: see CLAUDE.md)
cmake --build --preset release
ctest --test-dir build/release --output-on-failure
```

Without Qt: `cmake --preset core && cmake --build --preset core && ctest --preset core`.

## Improvements to do
- detect changes made outside the application (the counts already follow the application's own actions)
- run long copies / deletions in the background
- list view (QTreeView) for very large differences
