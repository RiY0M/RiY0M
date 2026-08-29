import os
import tkinter as tk
from tkinter import ttk

from Objects.ComparisonResult import ComparisonResult
from Managers.FolderManager import FolderManager
from Managers.FileManager import FileManager

from Views.ToolTip import ToolTip


class App:

    FONT_FAMILY = "Arial"

    SUMMARY_LABELS = {
        "total": {
            "color": "#000000",
            "result_name": "total_count",
            "label": "Total",
            "display": None,
        },
        "identical": {
            "label": "Identical",
            "result_name": "identical_count",
            "color": "#008000",
            "bg_color": "#E8F5E9",
            "display": None,
        },
        "all_differences": {
            "label": "All differences",
            "result_name": "all_differences_count",
            "color": "#B88600",
            "display": None,
        },
        "different": {
            "label": "Different",
            "result_name": "different_count",
            "color": "#B88600",
            "bg_color": "#FFF8E1",
            "display": None,
        },
        "only_a": {
            "label": "Only A",
            "result_name": "only_a_count",
            "color": "#E07000",
            "bg_color": "#FFF3E0",
            "display": None,
        },
        "only_b": {
            "label": "Only B",
            "result_name": "only_b_count",
            "color": "#CC0000",
            "bg_color": "#FFEBEE",
            "display": None,
        },
    }

    FILTERS = {
        "all_differences": {
            "label": "All",
            "result_name": "all_differences",
            "files": set(),
            "display": None,
        },
        "different": {
            "label": "Different",
            "result_name": "different",
            "color": SUMMARY_LABELS["different"]["color"],
            "bg_color": SUMMARY_LABELS["different"]["bg_color"],
            "files": set(),
            "display": None,
        },
        "only_a": {
            "label": "Only A",
            "result_name": "only_a",
            "color": SUMMARY_LABELS["only_a"]["color"],
            "bg_color": SUMMARY_LABELS["only_a"]["bg_color"],
            "files": set(),
            "display": None,
        },
        "only_b": {
            "label": "Only B",
            "result_name": "only_b",
            "color": SUMMARY_LABELS["only_b"]["color"],
            "bg_color": SUMMARY_LABELS["only_b"]["bg_color"],
            "files": set(),
            "display": None,
        },
    }
    DEFAULT_FILTER = "all_differences"

    FILE_ACTIONS = {
        "copy" : {
            "label": "copied",
            "color": SUMMARY_LABELS["identical"]["color"],
            "bg_color": SUMMARY_LABELS["identical"]["bg_color"],
            "files": set(),
        },
        "move" : {
            "label": "moved",
            "color": SUMMARY_LABELS["identical"]["color"],
            "bg_color": SUMMARY_LABELS["identical"]["bg_color"],
            "files": set(),
        },
        "skip" : {
            "label": "skipped",
            "color": SUMMARY_LABELS["different"]["color"],
            "bg_color": SUMMARY_LABELS["different"]["bg_color"],
            "files": set(),
        },
        "delete" : {
            "label": "deleted",
            "color": "#CC0000",
            "bg_color": "#FFEBEE",
            "files": set(),
        },
    }


    def __init__(self, root):

        self.root = root
        self.root.title("Folder Comparison")
        self.root.geometry("1050x750")
        self.root.minsize(850, 600)

        self.folder_a = tk.StringVar()
        self.folder_b = tk.StringVar()
        self.deep_comparison = tk.BooleanVar(value=False)
        self.recursive = tk.BooleanVar(value=True)

        self.result = ComparisonResult()
        self.file_container = None
        self.progress_area_items = []
        self.folder_area_items = []

        self.reset_filter()
        self.build_ui()

    # =========================================================
    # Main UI (done)
    # =========================================================

    def build_ui(self):

        self.build_folder_selection()
        self.build_progress_area()
        self.build_summary()
        self.build_filter_buttons()
        self.build_file_list()

    # =========================================================
    # Folder selection (done)
    # =========================================================

    def build_folder_selection(self):

        frame = ttk.LabelFrame(
            self.root,
            text="Folders",
            padding=10
        )

        frame.pack(
            fill="x",
            padx=10,
            pady=10
        )

        self.build_folder_line(frame, 0, 0, "Folder A:", self.folder_a, "Select Folder A")
        self.build_folder_line(frame, 1, 0, "Folder B:", self.folder_b, "Select Folder B")

    def build_folder_line(self, frame, row, column, label, textvariable, title):
        label = ttk.Label(
            frame,
            text=label
        )

        label.grid(
            row=row,
            column=column,
            sticky="w",
            pady=5
        )

        entry = ttk.Entry(
            frame,
            textvariable=textvariable,
            width=80
        )

        entry.grid(
            row=row,
            column=column+1,
            padx=5,
            sticky="ew"
        )

        button = ttk.Button(
            frame,
            text="Browse...",
            command=lambda: self.browse_folder(title, textvariable)
        )

        button.grid(
            row=row,
            column=column+2
        )

        self.folder_area_items.append(entry)
        self.folder_area_items.append(button)

    def browse_folder(self, title, variable):

        folder = tk.filedialog.askdirectory(title=title)

        if folder:
            variable.set(folder)

    def update_folder_area(self, state):
        for items in self.folder_area_items:
            items.config(state=state)

    # =========================================================
    # Progress (Done)
    # =========================================================

    def build_progress_area(self):

        frame = ttk.Frame(
            self.root
        )

        frame.pack(
            fill="x",
            padx=10,
            pady=5
        )

        compare_button = self.build_comparison_button(frame, "Compare")

        recursive_checkbox = self.build_progress_checkbox(
            frame, "Recursive", self.recursive,
            "When enabled, files inside all subfolders are included.\n"
            "When disabled, only files directly inside the selected\n"
            "folders are compared."
        )

        deep_comparison_checkbox = self.build_progress_checkbox(
            frame, "Deep comparison", self.deep_comparison,
            "When enabled, files will be compared throught name and content.\n"
            "When disabled, files will be compared throught name and size.\n"
            "TLDR: enabled = more accurate, disabled = faster."
        )

        self.progress_area_items.append(compare_button)
        self.progress_area_items.append(recursive_checkbox)
        self.progress_area_items.append(deep_comparison_checkbox)

        self.progress = ttk.Progressbar(
            frame,
            orient="horizontal",
            mode="determinate"
        )

        self.progress.pack(
            side="left",
            fill="x",
            expand=True
        )

        self.progress_label = ttk.Label(
            frame,
            text="Ready"
        )

        self.progress_label.pack(
            side="left",
            padx=(10, 0)
        )

    def build_comparison_button(self, frame, text):
        button = ttk.Button(
            frame,
            text=text,
            command=lambda: self.compare_folders(self.deep_comparison.get(), self.recursive.get())
        )

        button.pack(
            side="left",
            padx=2
        )

        return button

    def build_progress_checkbox(self, frame, label, variable, tooltip):
        checkbox = ttk.Checkbutton(
            frame,
            text=label,
            variable=variable
        )

        checkbox.pack(
            side="left",
            padx=(0, 15)
        )

        if tooltip:
            ToolTip(checkbox, tooltip)

        return checkbox

    def update_progress_area(self, state):
        for items in self.progress_area_items:
            items.config(state=state)

    # =========================================================
    # Summary (Done)
    # =========================================================

    def build_summary(self):

        LABEL_FONT = (self.FONT_FAMILY, 11, "bold")

        frame = ttk.LabelFrame(
            self.root,
            text="Summary",
            padding=10
        )

        frame.pack(
            fill="x",
            padx=10,
            pady=5
        )

        for summary_label in self.SUMMARY_LABELS.values():
            summary_label["display"] = self.build_summary_label(frame, summary_label["color"], LABEL_FONT)

        self.update_summary()

    def build_summary_label(self, frame, color, font):
        label = tk.Label(
            frame,
            fg=color,
            font=font
        )

        label.pack(
            side="left",
            padx=15
        )

        return label

    def update_summary(self):

        for summary_label in self.SUMMARY_LABELS.values():
            if summary_label["display"]:
                summary_label["display"].config(
                    text=f'{summary_label["label"]}: {getattr(self.result, summary_label["result_name"]) or 0}'
                )

    # =========================================================
    # Filter buttons (Done)
    # =========================================================

    def build_filter_buttons(self):

        frame = ttk.Frame(
            self.root
        )

        frame.pack(
            fill="x",
            padx=10,
            pady=(5, 0)
        )

        ttk.Label(
            frame,
            text="Show:"
        ).pack(
            side="left",
            padx=(0, 8)
        )

        for filter_name in self.FILTERS.keys():
            self.FILTERS[filter_name]["display"] = self.build_filter_button(frame, filter_name)

        self.update_filter_buttons()

    def build_filter_button(self, frame, filter_name):
        button = ttk.Button(
            frame,
            command=lambda: self.set_filter(filter_name)
        )

        button.pack(
            side="left",
            padx=2
        )

        return button

    def update_filter_buttons(self):

        for filter_name, button_info in self.FILTERS.items():
            if button_info["display"]:
                button_info["display"].config(
                    text=("● " if filter_name == self.current_filter else "") + button_info["label"]
                )

    # =========================================================
    # Filters (Done)
    # =========================================================

    def set_filter(self, filter_name):
        self.current_filter = filter_name

        self.update_filter_buttons()

        if self.file_container:
            self.populate_file_list(self.result, self.current_filter)
    
    def reset_filter(self):
        self.set_filter(self.DEFAULT_FILTER)

    # =========================================================
    # File list
    # =========================================================

    def build_file_list(self):

        frame = ttk.LabelFrame(
            self.root,
            text="Files",
            padding=5
        )

        frame.pack(
            fill="both",
            expand=True,
            padx=10,
            pady=5
        )

        self.canvas = tk.Canvas(
            frame,
            highlightthickness=0
        )

        self.scrollbar = ttk.Scrollbar(
            frame,
            orient="vertical",
            command=self.canvas.yview
        )

        self.file_container = ttk.Frame(
            self.canvas
        )

        self.file_container.bind(
            "<Configure>",
            lambda event: self.canvas.configure(
                scrollregion=self.canvas.bbox("all_differences")
            )
        )

        self.canvas_window = self.canvas.create_window(
            (0, 0),
            window=self.file_container,
            anchor="nw"
        )

        self.canvas.configure(
            yscrollcommand=self.scrollbar.set
        )

        self.canvas.pack(
            side="left",
            fill="both",
            expand=True
        )

        self.scrollbar.pack(
            side="right",
            fill="y"
        )

        self.canvas.bind(
            "<Configure>",
            self.resize_file_container
        )

        self.canvas.bind_all(
            "<MouseWheel>",
            self.on_mousewheel
        )

    def resize_file_container(self, event):

        self.canvas.itemconfig(
            self.canvas_window,
            width=event.width
        )

    def on_mousewheel(self, event):

        self.canvas.yview_scroll(
            int(-1 * (event.delta / 120)),
            "units"
        )

    # =========================================================
    # Compare
    # =========================================================

    def get_folder_path(self, folder):
        folder_path = folder.get().strip()

        if folder_path == "":
            tk.messagebox.showerror(
                "Invalid folder",
                "Folder must not be empty."
            )
            return

        folder_path = FolderManager.normalize_folder_path(
            folder.get().strip()
        )

        if not FolderManager.is_folder_path_valid(folder_path):
            tk.messagebox.showerror(
                "Invalid folder",
                "Folder does not exist."
            )
            return

        return folder_path

    def compare_folders(self, deep_comparison, recursive):

        self.folder_a_path = self.get_folder_path(self.folder_a)
        self.folder_b_path = self.get_folder_path(self.folder_b)

        if FolderManager.check_same_folders(self.folder_a_path, self.folder_b_path):

            tk.messagebox.showerror(
                "Invalid folders",
                "Folder A and Folder B are the same folder."
            )

            return

        # -----------------------------------------------------
        # Reset state
        # -----------------------------------------------------

        self.result = ComparisonResult()

        for file_action in self.FILE_ACTIONS.values():
            file_action["files"].clear()

        self.clear_file_list()


        self.is_scanning()

        self.result = FolderManager.compare(
            self.folder_a_path,
            self.folder_b_path,
            deep_comparison,
            recursive,
            progress_callback=self.comparison_progress
        )

        self.scan_complete()

    def comparison_progress(
        self,
        current,
        total,
        filename
    ):

        self.progress["maximum"] = max(
            total,
            1
        )

        self.progress["value"] = current

        self.progress_label.config(
            text=f"{current} / {total}"
        )

        self.root.update()

    def is_scanning(self):
        self.update_folder_area("disabled")
        self.update_progress_area("disabled")
        self.progress["value"] = 0
        self.progress_label.config(text="Scanning...")
        self.root.update()

    def scan_complete(self):
        self.update_summary()
        self.populate_file_list(self.result, self.current_filter)
        self.progress_label.config(text="Comparison complete")

    # =========================================================
    # File list filtering (Done)
    # =========================================================

    def populate_file_list(self, result, current_filter):

        self.clear_file_list()

        if not result:
            return

        # TODO begin : remove this bloc
        # Color problem if we don't do this because result doesn't specify to
        # which group the file belongs to other than being in the correct list,
        # which is an issue when there are merged

        sepecific_case = "all_differences"
        if current_filter == sepecific_case:
            other_filters = dict(self.FILTERS)
            del other_filters[sepecific_case]

            for filter_name in other_filters:
                self.populate_file_list(result, filter_name)
            return
        # TODO end

        for filter_name, filter_info in self.FILTERS.items():
            if current_filter == filter_name and len(filter_info["files"]) == 0:
                for path in getattr(result, filter_name):

                    filter_info["files"].add(
                        (path, filter_name)
                    )

                break


        for path, file_type in self.FILTERS[current_filter]["files"]:

            self.create_file_row(
                path,
                file_type
            )

    def clear_file_list(self):

        for widget in self.file_container.winfo_children():
            widget.destroy()

    # =========================================================
    # File row (Done)
    # =========================================================

    def build_row(self, frame, path, status, color, background):
        row = tk.Frame(
            frame,
            bg=background,
            bd=1,
            relief="solid"
        )

        row.pack(
            fill="x",
            padx=2,
            pady=2
        )

        status_label = tk.Label(
            row,
            text=status,
            fg=color,
            bg=background,
            width=12,
            anchor="w",
            font=(self.FONT_FAMILY, 9, "bold")
        )

        status_label.pack(
            side="left",
            padx=8
        )

        filename_label = tk.Label(
            row,
            text=path,
            fg=color,
            bg=background,
            anchor="w",
            justify="left",
            font=(self.FONT_FAMILY, 10)
        )

        filename_label.pack(
            side="left",
            fill="x",
            expand=True,
            padx=5,
            pady=7
        )

        return row

    def build_row_buttons(self, frame, label, command):
        button = tk.Button(
            frame,
            text=label,
            command=command
        )

        button.pack(
            side="right",
            padx=5
        )

        return button

    def create_file_row(
        self,
        path,
        file_type
    ):

        found_in_actions = False
        found_in_files = False
        color = None
        background = None
        status = None

        for file_action in self.FILE_ACTIONS.values():
            if found_in_actions:
                break

            if path in file_action["files"] and "color" in file_action:

                color = file_action["color"]
                background = file_action["bg_color"]
                status = file_action["label"].upper()
                found_in_actions = True

        for filter_name, filter_value in self.FILTERS.items():
            if found_in_actions or found_in_files:
                break

            if file_type == filter_name and "color" in filter_value:

                color = filter_value["color"]
                background = filter_value["bg_color"]
                status = filter_value["label"].upper()
                found_in_files = True

        row = self.build_row(self.file_container, path, status, color, background)

        if found_in_actions:
            return

        # TODO replace by this we need to make proper objects
        # self.build_row_buttons(row, label, command)

        # -----------------------------------------------------
        # Only A
        # -----------------------------------------------------

        if file_type == "only_a":

            button = tk.Button(
                row,
                text="Copy → B",
                command=lambda:
                    self.copy_path(
                        path,
                        self.folder_a_path,
                        self.folder_b_path
                    )
            )

            button.pack(
                side="right",
                padx=5
            )

        # -----------------------------------------------------
        # Only B
        # -----------------------------------------------------

        elif file_type == "only_b":

            button = tk.Button(
                row,
                text="Copy → A",
                command=lambda:
                    self.copy_path(
                        path,
                        self.folder_b_path,
                        self.folder_a_path
                    )
            )

            button.pack(
                side="right",
                padx=5
            )

        # -----------------------------------------------------
        # Different
        # -----------------------------------------------------

        elif file_type == "different":

            button_b = tk.Button(
                row,
                text="A → B",
                command=lambda:
                    self.copy_path(
                        path,
                        self.folder_a_path,
                        self.folder_b_path
                    )
            )

            button_b.pack(
                side="right",
                padx=3
            )

            button_a = tk.Button(
                row,
                text="B → A",
                command=lambda:
                    self.copy_path(
                        path,
                        self.folder_b_path,
                        self.folder_a_path
                    )
            )

            button_a.pack(
                side="right",
                padx=3
            )

    # =========================================================
    # Copy (Done)
    # =========================================================

    def copy_path(
        self,
        path,
        source_folder,
        destination_folder
    ):

        source =      os.path.join(source_folder,      path)
        destination = os.path.join(destination_folder, path)

        try:
            FileManager.copy_file(
                source,
                destination
            )

        except Exception as error:
            tk.messagebox.showerror(
                "Copy error",
                f"Could not copy:\n\n"
                f"{path}\n\n"
                f"{error}"
            )
            return

        self.FILE_ACTIONS["copy"]["files"].add((path, )) # TODO : add tuple with action name like the filters add their
        self.populate_file_list(self.result, self.current_filter) # TODO : change to rebuild only concerned line
