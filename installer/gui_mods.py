"""The Mods window of the installer (installer/gui.py, the Mods button): a list of the mod library joined with the
mods in the install folder, and buttons to install, update, turn on, turn off and remove them. installer/mods.py does
the work; downloads run on a worker thread and report back through a queue the window polls."""
import queue
import threading
import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import mods, theme

POLL_MS = 100
PAD = 14


class ModsWindow:
    def __init__(self, parent, cat, dest, ui_font, index=None):
        self.cat, self.dest, self.index_source = cat, Path(dest), index
        self.entries = []
        self.busy = False
        self.results = queue.Queue()
        w = self.win = tk.Toplevel(parent)
        w.title(cat('mods.title'))
        w.transient(parent)
        w.minsize(640, 420)
        w.configure(background=theme.BG)
        theme.dark_title_bar(w)
        f = ttk.Frame(w, padding=PAD)
        f.pack(fill='both', expand=True)
        ttk.Label(f, text=cat('mods.intro', dest=str(self.dest)), wraplength=700, justify='left').pack(anchor='w')

        bar = ttk.Frame(f)   # packed before the list so the list shrinks, never the buttons
        bar.pack(side='bottom', fill='x', pady=(10, 0))
        self.buttons = {}
        for key, side in (('install', 'left'), ('toggle', 'left'), ('remove', 'left'), ('close', 'right'),
                          ('add_zip', 'right'), ('refresh', 'right')):
            b = ttk.Button(bar, text=cat('mods.' + key), command=getattr(self, 'on_' + key))
            b.pack(side=side, padx=(0, 8) if side == 'left' else (8, 0))
            self.buttons[key] = b
        self.status = ttk.Label(f, wraplength=700, justify='left')
        self.status.pack(side='bottom', anchor='w', pady=(8, 0))

        detail = ttk.Frame(f)
        detail.pack(side='bottom', fill='x', pady=(8, 0))
        self.detail = ttk.Label(detail, wraplength=700, justify='left')
        self.detail.pack(anchor='w')
        self.page = ttk.Button(detail, text=cat('mods.page'), command=self.on_page)

        cols = ('name', 'kind', 'author', 'state')
        box = ttk.Frame(f)
        box.pack(fill='both', expand=True, pady=(10, 0))
        self.tree = ttk.Treeview(box, columns=cols, show='headings', selectmode='browse', height=10)
        for c, width in zip(cols, (260, 110, 140, 150)):
            self.tree.heading(c, text=cat('mods.col_' + c))
            self.tree.column(c, width=width, stretch=c == 'name')
        scroll = ttk.Scrollbar(box, orient='vertical', command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.pack(side='left', fill='both', expand=True)
        scroll.pack(side='right', fill='y')
        self.tree.bind('<<TreeviewSelect>>', lambda _e: self.refresh_selection())
        w.protocol('WM_DELETE_WINDOW', self.on_close)
        self.refresh_selection()
        self.load()
        w.after(POLL_MS, self.poll)
        w.update_idletasks()   # over the Setup window, not in a corner of the screen
        x = parent.winfo_rootx() + (parent.winfo_width() - w.winfo_reqwidth()) // 2
        y = parent.winfo_rooty() + (parent.winfo_height() - w.winfo_reqheight()) // 2
        w.geometry(f'+{max(0, x)}+{max(0, y)}')

    # ---- worker --------------------------------------------------------------------------------------------------

    def run(self, message, work):
        """Runs work() on a worker thread; its result (or ModError) comes back to done()."""
        if self.busy:
            return
        self.busy = True
        self.set_status(message)
        self.refresh_selection()

        def body():
            try:
                self.results.put(('ok', work()))
            except mods.ModError as e:
                self.results.put(('error', e))
            except OSError as e:
                self.results.put(('error', mods.ModError('mods.failed', detail=str(e))))
        threading.Thread(target=body, daemon=True).start()

    def poll(self):
        try:
            while True:
                kind, value = self.results.get_nowait()
                self.busy = False
                if kind == 'progress':
                    self.busy = True
                    self.set_status(self.cat('mods.downloading', done=value[0] >> 20, total=max(1, value[1] >> 20)))
                    continue
                if kind == 'error':
                    self.set_status(self.cat(value.id, **value.params), error=True)
                else:
                    if isinstance(value, list):
                        self.entries = value
                        if self.status.cget('foreground') != theme.ERROR:
                            self.set_status('')   # the "reading" or "installing" line is done
                    elif isinstance(value, str):
                        self.set_status(value)
                    self.fill()
                self.refresh_selection()
        except queue.Empty:
            pass
        if self.win.winfo_exists():
            self.win.after(POLL_MS, self.poll)

    def progress(self, done, total):
        self.results.put(('progress', (done, total)))

    def load(self):
        def work():
            try:
                index = mods.fetch_index(self.index_source)
            except mods.ModError:
                index = []   # offline: still list and switch the installed mods
                self.results.put(('error', mods.ModError('mods.offline')))
            return mods.status(self.dest, index)
        self.run(self.cat('mods.loading'), work)

    # ---- list ----------------------------------------------------------------------------------------------------

    def fill(self):
        keep = self.selected_id()
        self.tree.delete(*self.tree.get_children())
        for e in self.entries:
            if not e['installed']:
                state = self.cat('mods.state_available')
            elif e.get('update'):
                state = self.cat('mods.state_update')
            else:
                state = self.cat('mods.state_on' if e['enabled'] else 'mods.state_off')
            self.tree.insert('', 'end', iid=e['id'], values=(e.get('name') or e['id'], self.cat('mods.kind_' + e['kind']),
                                                             e.get('author', ''), state))
        if keep and self.tree.exists(keep):
            self.tree.selection_set(keep)

    def selected_id(self):
        sel = self.tree.selection()
        return sel[0] if sel else None

    def selected(self):
        mid = self.selected_id()
        return next((e for e in self.entries if e['id'] == mid), None)

    def refresh_selection(self):
        e = self.selected()
        b = self.buttons
        state = lambda on: ['!disabled'] if on and not self.busy else ['disabled']
        in_library = bool(e and e.get('url') or e and e.get('files'))
        b['install'].configure(text=self.cat('mods.update' if e and e['installed'] else 'mods.install'))
        b['install'].state(state(e and in_library and (not e['installed'] or e.get('update'))))
        b['toggle'].configure(text=self.cat('mods.turn_off' if e and e['enabled'] else 'mods.turn_on'))
        b['toggle'].state(state(e and e['installed']))
        b['remove'].state(state(e and e['installed']))
        b['add_zip'].state(state(True))
        b['refresh'].state(state(True))
        if not e:
            self.detail.configure(text=self.cat('mods.pick'))
            self.page.pack_forget()
            return
        text = e.get('description') or ''
        if e.get('author'):
            text += ('\n' if text else '') + self.cat('mods.by', author=e['author'])
        if e.get('notes'):
            text += '\n' + e['notes']
        if e['kind'] == 'lua':
            text += '\n' + self.cat('mods.lua_restart')
        self.detail.configure(text=text)
        if e.get('homepage'):
            self.page.pack(anchor='w', pady=(6, 0))
        else:
            self.page.pack_forget()

    def set_status(self, text, error=False):
        self.status.configure(text=text, foreground=theme.ERROR if error else '')

    # ---- buttons -------------------------------------------------------------------------------------------------

    def on_install(self):
        e = self.selected()
        if not e:
            return
        if e['kind'] == 'romfs' and not messagebox.askokcancel(
                self.cat('mods.title'), self.cat('mods.confirm_download', name=e['name'], url=e['homepage'],
                                                 size=max(1, int(e['size']) >> 20)), parent=self.win):
            return
        entry = {k: v for k, v in e.items() if k not in ('installed', 'enabled', 'update')}
        self.run(self.cat('mods.installing', name=e['name']),
                 lambda: (mods.install(self.dest, entry, self.progress), self.reload_now())[1])

    def on_toggle(self):
        e = self.selected()
        if not e:
            return
        on = not e['enabled']
        self.run('', lambda: (mods.set_enabled(self.dest, e['id'], on), self.reload_now())[1])

    def on_remove(self):
        e = self.selected()
        if not e or not messagebox.askyesno(self.cat('mods.title'), self.cat('mods.confirm_remove', name=e['name']),
                                            parent=self.win):
            return
        self.run('', lambda: (mods.remove(self.dest, e['id']), self.reload_now())[1])

    def on_add_zip(self):
        path = filedialog.askopenfilename(parent=self.win, title=self.cat('mods.add_zip'),
                                          filetypes=[(self.cat('mods.type_zip'), '*.zip')])
        if path:
            self.run(self.cat('mods.installing', name=Path(path).name),
                     lambda: (mods.add_zip(self.dest, path), self.reload_now())[1])

    def on_refresh(self):
        self.load()

    def on_page(self):
        e = self.selected()
        if e and str(e.get('homepage', '')).startswith('https://'):
            webbrowser.open(e['homepage'])

    def on_close(self):
        self.win.destroy()

    def reload_now(self):
        """The list after a change, on the worker thread (the library entries stay as they were)."""
        index = [{k: v for k, v in e.items() if k not in ('installed', 'enabled', 'update')}
                 for e in self.entries if e.get('url') or e.get('files')]
        return mods.status(self.dest, index)
