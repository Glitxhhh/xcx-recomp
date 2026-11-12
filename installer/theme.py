"""The look of XCX Setup: the dark navy, teal and cyan of the game's F1 menu (runtime/ui/menu.cpp, style_init), on
Tk's clam theme, which takes colours on every ttk widget (the Windows themes ignore most of them)."""
import os
import tkinter as tk
from tkinter import ttk

# runtime/ui/menu.cpp, style_init, as 8-bit colours
BG = '#08101a'          # WindowBg
PANEL = '#0f1f2e'       # Tab: fields, lists, the log
BUTTON = '#12263a'      # Button
HOVER = '#1c5473'       # ButtonHovered, TabHovered
PRESSED = '#1a80a3'     # ButtonActive
SELECTED = '#1a4a69'    # TabSelected: list selection, a picked toolbutton
CYAN = '#59dbf5'        # NavCursor, TabSelectedOverline: focus, progress
TEXT = '#ebf5ff'
DIM = '#8c9ead'         # TextDisabled
LINE = '#33526a'        # Separator: borders

# the state colours of gui.py and gui_mods.py, light enough to read on BG
OK = '#57d38c'
RUN = CYAN
ERROR = '#ff7b7b'
WARN = '#f5b942'
NOTICE_BG, NOTICE_FG = '#2a2412', '#f5d67a'


def apply(root, font):
    """Styles root and every ttk widget made after it; returns the ttk.Style."""
    s = ttk.Style(root)
    s.theme_use('clam')
    base = (font, 10)
    root.configure(background=BG)
    s.configure('.', background=BG, foreground=TEXT, fieldbackground=PANEL, bordercolor=LINE, darkcolor=BG,
                lightcolor=BG, troughcolor=PANEL, focuscolor=CYAN, selectbackground=SELECTED,
                selectforeground=TEXT, insertcolor=TEXT, font=base)
    s.map('.', foreground=[('disabled', DIM)])
    s.configure('TFrame', background=BG)
    s.configure('TLabel', background=BG, foreground=TEXT)
    s.configure('TButton', background=BUTTON, foreground=TEXT, bordercolor=LINE, lightcolor=BUTTON,
                darkcolor=BUTTON, padding=(14, 6), relief='flat')
    s.map('TButton', background=[('disabled', BG), ('pressed', PRESSED), ('active', HOVER)],
          bordercolor=[('focus', CYAN)], lightcolor=[('pressed', PRESSED), ('active', HOVER)],
          darkcolor=[('pressed', PRESSED), ('active', HOVER)], foreground=[('disabled', DIM)])
    s.configure('Toolbutton', background=BG, foreground=DIM, padding=(8, 3), relief='flat')
    s.map('Toolbutton', background=[('selected', SELECTED), ('active', HOVER)],
          foreground=[('selected', TEXT), ('active', TEXT)])
    for w in ('TEntry', 'TCombobox'):
        s.configure(w, fieldbackground=PANEL, foreground=TEXT, bordercolor=LINE, lightcolor=PANEL, darkcolor=PANEL,
                    arrowcolor=CYAN, padding=4)
        s.map(w, bordercolor=[('focus', CYAN)], lightcolor=[('focus', PANEL)],
              fieldbackground=[('readonly', PANEL), ('disabled', BG)], foreground=[('disabled', DIM)],
              selectbackground=[('readonly', PANEL)], selectforeground=[('readonly', TEXT)])
    s.configure('TCombobox', background=BUTTON)
    s.map('TCombobox', background=[('active', HOVER)])
    for w in ('TCheckbutton', 'TRadiobutton'):
        s.configure(w, background=BG, foreground=TEXT, indicatorbackground=PANEL, indicatorforeground=CYAN,
                    upperbordercolor=LINE, lowerbordercolor=LINE)
        s.map(w, background=[('active', BG)], indicatorbackground=[('selected', PANEL), ('active', HOVER)])
    s.configure('Horizontal.TProgressbar', background=CYAN, troughcolor=PANEL, bordercolor=LINE, lightcolor=CYAN,
                darkcolor=PRESSED)
    s.configure('Vertical.TScrollbar', background=BUTTON, troughcolor=BG, bordercolor=BG, arrowcolor=CYAN,
                lightcolor=BUTTON, darkcolor=BUTTON)
    s.map('Vertical.TScrollbar', background=[('active', HOVER)])
    s.configure('Treeview', background=PANEL, fieldbackground=PANEL, foreground=TEXT, bordercolor=LINE, rowheight=26)
    s.map('Treeview', background=[('selected', SELECTED)], foreground=[('selected', TEXT)])
    s.configure('Treeview.Heading', background=BUTTON, foreground=CYAN, bordercolor=LINE, lightcolor=BUTTON,
                darkcolor=BUTTON, relief='flat', font=(font, 10, 'bold'))
    s.map('Treeview.Heading', background=[('active', HOVER)])
    # the Combobox's drop-down list is a plain Tk listbox
    root.option_add('*TCombobox*Listbox.background', PANEL)
    root.option_add('*TCombobox*Listbox.foreground', TEXT)
    root.option_add('*TCombobox*Listbox.selectBackground', SELECTED)
    root.option_add('*TCombobox*Listbox.selectForeground', TEXT)
    root.option_add('*TCombobox*Listbox.font', base)
    return s


def text_widget(w):
    """A tk.Text (the run log) in the theme's colours."""
    w.configure(background=PANEL, foreground=TEXT, insertbackground=TEXT, selectbackground=SELECTED,
                highlightthickness=1, highlightbackground=LINE, highlightcolor=CYAN, relief='flat', borderwidth=0)


def dark_title_bar(window):
    """Windows 10 1809 and later: a dark title bar to go with the window. Elsewhere, or when it fails, nothing."""
    if os.name != 'nt':
        return
    try:
        import ctypes
        window.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(window.winfo_id())
        on = ctypes.c_int(1)
        for attribute in (20, 19):   # DWMWA_USE_IMMERSIVE_DARK_MODE, and its number before Windows 10 20H1
            if not ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(on), ctypes.sizeof(on)):
                break
    except (OSError, AttributeError, tk.TclError):
        pass
