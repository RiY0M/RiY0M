import os

from ComparisonResult import ComparisonResult
from FileManager import FileManager


class FolderManager:

    @staticmethod
    def compare(
        folder_a,
        folder_b,
        deep_comparison,
        recursive,
        progress_callback=None
    ):

        result = ComparisonResult()

        files_a = FileManager.get_files(folder_a, recursive)
        files_b = FileManager.get_files(folder_b, recursive)

        result.only_a = sorted(files_a - files_b)
        result.only_b = sorted(files_b - files_a)
        common        = sorted(files_a & files_b)

        total = len(common)

        for index, path in enumerate(
            common,
            start=1
        ):

            if progress_callback:

                progress_callback(
                    index,
                    total,
                    path
                )

            file_a = os.path.join(folder_a, path)
            file_b = os.path.join(folder_b, path)

            if FileManager.files_are_identical(
                file_a,
                file_b,
                deep_comparison
            ):

                result.identical.append(path)

            else:

                result.different.append(path)

        return result

    @staticmethod
    def normalize_folder_path(folder_path):
        return os.path.normcase(
            os.path.normpath(
                os.path.abspath(
                    folder_path
                )
            )
        )

    @staticmethod
    def is_folder_path_valid(folder_path):
        if folder_path == "":
            return False
        return os.path.isdir(FolderManager.normalize_folder_path(folder_path))

    @staticmethod
    def check_same_folders(folder_a, folder_b):
        return os.path.samefile(
            FolderManager.normalize_folder_path(folder_a),
            FolderManager.normalize_folder_path(folder_b)
        )
