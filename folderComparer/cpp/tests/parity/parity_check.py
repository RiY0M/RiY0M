#!/usr/bin/env python3
"""Parity test: the C++ comparison must give exactly the same result as the
original Python FolderManager.compare, for every option combination.

Random folder trees are generated (nested folders, unicode names, files with
the same size but other content, names whose sort order differs between
string and path ordering...), compared by both implementations, and the four
lists (identical, different, only_a, only_b) are checked element by element,
order included.

Usage: parity_check.py --cli <folder_comparer_cli> --python-sources <dir>
"""

from __future__ import annotations

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

CATEGORIES = ("identical", "different", "only_a", "only_b")
NAMES = ["a", "b", "a-c", "a.b", "Zed", "zed", "café", "日本", "data 1", "x_y", "_hidden"]
OPTION_SETS = [
    (False, True),
    (True, True),
    (False, False),
    (True, False),
]


def random_relative_path(rng: random.Random) -> str:
    depth = rng.choice([0, 0, 1, 1, 2, 3])
    parts = [rng.choice(NAMES) for _ in range(depth)]
    parts.append(rng.choice(NAMES) + rng.choice(["", ".txt", ".bin", "-1"]))
    return os.path.join(*parts)


def write(path: Path, content: bytes) -> bool:
    """Write a file, unless it collides with an existing folder/file layout."""
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.is_dir():
            return False
        path.write_bytes(content)
        return True
    except (NotADirectoryError, FileExistsError, IsADirectoryError):
        return False


def build_tree(root: Path, rng: random.Random, file_count: int) -> None:
    folder_a = root / "A"
    folder_b = root / "B"
    folder_a.mkdir()
    folder_b.mkdir()

    for _ in range(file_count):
        relative = random_relative_path(rng)
        size = rng.choice([0, 1, 5, 64, 4096])
        content = bytes(rng.getrandbits(8) for _ in range(size))
        kind = rng.choice(["same", "same", "size", "content", "only_a", "only_b"])

        if kind == "only_a":
            write(folder_a / relative, content)
        elif kind == "only_b":
            write(folder_b / relative, content)
        elif kind == "same":
            if write(folder_a / relative, content):
                write(folder_b / relative, content)
        elif kind == "size":
            if write(folder_a / relative, content):
                write(folder_b / relative, content + b"!")
        elif kind == "content":
            if not content:
                content = b"x"
            other = bytes((byte + 1) % 256 for byte in content)
            if write(folder_a / relative, content):
                write(folder_b / relative, other)


def python_result(folder_a: Path, folder_b: Path, deep: bool, recursive: bool) -> dict[str, list[str]]:
    from Managers.FolderManager import FolderManager  # noqa: PLC0415 (imported after sys.path setup)

    result = FolderManager.compare(str(folder_a), str(folder_b), deep, recursive)
    return {
        "identical": [path.replace(os.sep, "/") for path in result.identical],
        "different": [path.replace(os.sep, "/") for path in result.different],
        "only_a": [path.replace(os.sep, "/") for path in result.only_a],
        "only_b": [path.replace(os.sep, "/") for path in result.only_b],
    }


def cpp_result(cli: str, folder_a: Path, folder_b: Path, deep: bool, recursive: bool) -> dict[str, list[str]]:
    command = [cli, str(folder_a), str(folder_b)]
    if deep:
        command.append("--deep")
    if not recursive:
        command.append("--no-recursive")

    completed = subprocess.run(command, capture_output=True, check=False)
    if completed.returncode not in (0, 1):
        raise RuntimeError(f"CLI failed ({completed.returncode}): {completed.stderr.decode(errors='replace')}")

    lists: dict[str, list[str]] = {category: [] for category in CATEGORIES}
    for line in completed.stdout.decode("utf-8").splitlines():
        category, _, path = line.partition("\t")
        lists[category].append(path)
    return lists


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", required=True, help="path to folder_comparer_cli")
    parser.add_argument("--python-sources", required=True, help="folder holding the Python Managers/ package")
    parser.add_argument("--seeds", type=int, default=25, help="number of random trees")
    parser.add_argument("--files", type=int, default=80, help="files per random tree")
    arguments = parser.parse_args()

    sys.path.insert(0, str(Path(arguments.python_sources).resolve()))
    sys.dont_write_bytecode = True

    failures = 0
    checks = 0
    for seed in range(arguments.seeds):
        rng = random.Random(seed)
        with tempfile.TemporaryDirectory(prefix="fc-parity-") as temporary:
            root = Path(temporary)
            build_tree(root, rng, arguments.files)
            for deep, recursive in OPTION_SETS:
                checks += 1
                expected = python_result(root / "A", root / "B", deep, recursive)
                actual = cpp_result(arguments.cli, root / "A", root / "B", deep, recursive)
                if expected != actual:
                    failures += 1
                    print(f"MISMATCH seed={seed} deep={deep} recursive={recursive}")
                    for category in CATEGORIES:
                        if expected[category] != actual[category]:
                            print(f"  {category}:\n    python: {expected[category]}\n    c++:    {actual[category]}")

    print(f"{checks - failures}/{checks} parity checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
