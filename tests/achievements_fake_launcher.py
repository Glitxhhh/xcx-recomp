"""Plays RecompLauncher's side of the Recomp Achievements channel (runtime/achievements.c) on Windows, to check the game's
messages without the launcher: creates the named pipe, starts xcx.exe with RL_SESSION naming it, prints every line the
game sends and asks one peek of the game data pointer once the game has said hello.

    python tests/achievements_fake_launcher.py --seconds 120 -- <xcx.exe> [its arguments]

Exit code 0 when the game said hello, else 1. Lines go to stdout as "game> {...}".
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import threading
import time

PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, PIPE_READMODE_BYTE, PIPE_WAIT = 3, 0, 0, 0
INVALID = wt.HANDLE(-1).value
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateNamedPipeW.restype = wt.HANDLE
k32.CreateNamedPipeW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.DWORD, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_void_p]
k32.ConnectNamedPipe.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.ReadFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]
k32.WriteFile.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, ctypes.POINTER(wt.DWORD), ctypes.c_void_p]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=120)
    ap.add_argument('--peek', type=lambda s: int(s, 0), help='guest address to peek after hello')
    ap.add_argument('game', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.game[1:] if a.game[:1] == ['--'] else a.game
    name = rf'\\.\pipe\RecompLauncher-test-{os.getpid()}'
    pipe = k32.CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                65536, 65536, 0, None)
    if pipe == INVALID:
        sys.exit(f'CreateNamedPipe failed: {ctypes.get_last_error()}')
    game = subprocess.Popen(cmd, env=dict(os.environ, RL_SESSION=name))
    seen = {'hello': False}
    lines = []

    def reader():
        if not k32.ConnectNamedPipe(pipe, None) and ctypes.get_last_error() != 535:   # 535: already connected
            return
        print('launcher> game connected', flush=True)
        buf, have = ctypes.create_string_buffer(4096), b''
        while True:
            got = wt.DWORD()
            if not k32.ReadFile(pipe, buf, 4096, ctypes.byref(got), None) or not got.value:
                print('launcher> channel closed', flush=True)
                return
            have += buf.raw[:got.value]
            while b'\n' in have:
                line, have = have.split(b'\n', 1)
                text = line.decode('utf-8', 'replace')
                print('game>', text, flush=True)
                lines.append(text)
                msg = json.loads(text)
                if msg.get('t') == 'hello':
                    seen['hello'] = True
                    send({'t': 'quiet'})
                    if a.peek is not None:
                        send({'t': 'peek', 'address': a.peek, 'size': 8, 'seq': 1})

    def send(obj):
        data = (json.dumps(obj) + '\n').encode()
        done = wt.DWORD()
        k32.WriteFile(pipe, data, len(data), ctypes.byref(done), None)
        print('launcher>', obj, flush=True)

    threading.Thread(target=reader, daemon=True).start()
    try:
        game.wait(timeout=a.seconds)
    except subprocess.TimeoutExpired:
        game.terminate()
        game.wait()
    time.sleep(0.5)
    print(f'launcher> {len(lines)} lines, hello {"seen" if seen["hello"] else "missing"}', flush=True)
    sys.exit(0 if seen['hello'] else 1)


if __name__ == '__main__':
    main()
