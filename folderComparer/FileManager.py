import hashlib
import os
import shutil
from datetime import datetime


class FileManager:

    @staticmethod
    def get_files(folder, recursive = True):
        """Return all files using paths relative to folder."""

        files = set()

        for root, _, filenames in os.walk(folder):

            for filename in filenames:

                full_path = os.path.join(root, filename)

                relative_path = os.path.relpath(
                    full_path,
                    folder
                )

                files.add(relative_path)

        return files

    @staticmethod
    def calculate_hash(filename):
        """Calculate SHA-256 hash of a file."""

        sha256 = hashlib.sha256()

        with open(filename, "rb") as file:

            while True:

                data = file.read(1024 * 1024)

                if not data:
                    break

                sha256.update(data)

        return sha256.hexdigest()

    @staticmethod
    def files_are_identical(file_a, file_b, deep_comparison = True):
        """Return True if two files have identical contents."""

        if os.path.getsize(file_a) != os.path.getsize(file_b):
            return False

        if deep_comparison:
            return FileManager.calculate_hash(file_a) == FileManager.calculate_hash(file_b)

        return True

    @staticmethod
    def copy_file(source, destination):
        """Copy a file while preserving metadata."""

        if not os.path.exists(source):
            raise NameError(f"Source file no longer exists:\n\n{source}")

        destination_directory = os.path.dirname(destination)

        os.makedirs(
            destination_directory,
            exist_ok=True
        )

        shutil.copy2(
            source,
            destination
        )

    @staticmethod
    def format_size(size):
        """Convert bytes to a human-readable size."""

        units = [
            "B",
            "KB",
            "MB",
            "GB",
            "TB"
        ]

        value = float(size)

        for unit in units:

            if value < 1024:
                return f"{value:.1f} {unit}"

            value /= 1024

        return f"{value:.1f} PB"

    @staticmethod
    def get_file_info(filename):
        """Return basic file information."""

        size = os.path.getsize(filename)

        modified = datetime.fromtimestamp(
            os.path.getmtime(filename)
        )

        return {
            "size": size,
            "size_text": FileManager.format_size(size),
            "modified": modified,
            "modified_text": modified.strftime(
                "%Y-%m-%d %H:%M:%S"
            )
        }
