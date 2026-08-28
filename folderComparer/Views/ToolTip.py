import tkinter as tk

class ToolTip:
    """Simple tooltip for Tkinter widgets."""

    def __init__(self, widget, text):

        self.widget = widget
        self.text = text
        self.tooltip = None

        widget.bind(
            "<Enter>",
            self.show
        )

        widget.bind(
            "<Leave>",
            self.hide
        )

    def show(self, event=None):

        if self.tooltip is not None:
            return

        x = (
            self.widget.winfo_rootx()
            + self.widget.winfo_width()
            + 5
        )

        y = (
            self.widget.winfo_rooty()
            + self.widget.winfo_height()
            + 5
        )

        self.tooltip = tk.Toplevel(
            self.widget
        )

        self.tooltip.wm_overrideredirect(
            True
        )

        self.tooltip.geometry(
            f"+{x}+{y}"
        )

        label = tk.Label(
            self.tooltip,
            text=self.text,
            justify="left",
            background="#ffffe0",
            relief="solid",
            borderwidth=1,
            padx=6,
            pady=4
        )

        label.pack()

    def hide(self, event=None):

        if self.tooltip is not None:

            self.tooltip.destroy()

            self.tooltip = None
