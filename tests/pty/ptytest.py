"""ptytest.py -- drive an interactive shell through a pseudo-terminal and
inspect what a real terminal would show.

Term is a small VT model (enough for what a line editor emits: cursor moves,
erase, wrap, UTF-8 with wide characters); Session runs a program on a pty and
feeds the bytes to it.
"""

import fcntl
import os
import pty
import select
import struct
import termios
import time
import unicodedata


class Term:
    def __init__(self, rows=24, cols=80):
        self.rows, self.cols = rows, cols
        self.grid = [[" "] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.wrap = False
        self.buf = b""

    def _scroll(self):
        self.grid.pop(0)
        self.grid.append([" "] * self.cols)
        self.r = self.rows - 1

    def _put(self, ch):
        w = 2 if unicodedata.east_asian_width(ch) in "WF" else \
            0 if unicodedata.combining(ch) else 1
        if w == 0:
            return
        if self.wrap or self.c + w > self.cols:
            self.c = 0
            self.r += 1
            if self.r >= self.rows:
                self._scroll()
            self.wrap = False
        self.grid[self.r][self.c] = ch
        if w == 2 and self.c + 1 < self.cols:
            self.grid[self.r][self.c + 1] = ""
        self.c += w
        if self.c >= self.cols:
            self.c = self.cols - 1
            self.wrap = True

    def feed(self, data):
        self.buf += data
        text = self.buf.decode("utf-8", "ignore")
        # keep an incomplete trailing UTF-8 sequence / escape for next time
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b":
                if i + 1 >= len(text):
                    break
                if text[i + 1] == "[":
                    j = i + 2
                    while j < len(text) and not ("@" <= text[j] <= "~"):
                        j += 1
                    if j >= len(text):
                        break
                    self._csi(text[i + 2:j], text[j])
                    i = j + 1
                    continue
                i += 2
                continue
            i += 1
            if ch == "\r":
                self.c = 0
                self.wrap = False
            elif ch == "\n":
                self.wrap = False
                self.r += 1
                if self.r >= self.rows:
                    self._scroll()
            elif ch == "\b":
                self.c = max(0, self.c - 1)
                self.wrap = False
            elif ch == "\a" or ch < " ":
                pass
            else:
                self._put(ch)
        self.buf = text[i:].encode()

    def _csi(self, params, final):
        priv = params.startswith("?")
        nums = [int(x) if x.isdigit() else 0 for x in params.lstrip("?").split(";")] if params else []
        n = nums[0] if nums and nums[0] else 1
        if priv:
            return
        self.wrap = False if final in "ABCDHJK" else self.wrap
        if final == "A":
            self.r = max(0, self.r - n)
        elif final == "B":
            self.r = min(self.rows - 1, self.r + n)
        elif final == "C":
            self.c = min(self.cols - 1, self.c + n)
        elif final == "D":
            self.c = max(0, self.c - n)
        elif final == "H":
            self.r = (nums[0] - 1) if nums and nums[0] else 0
            self.c = (nums[1] - 1) if len(nums) > 1 and nums[1] else 0
        elif final == "J":
            mode = nums[0] if nums else 0
            if mode == 2:
                self.grid = [[" "] * self.cols for _ in range(self.rows)]
            else:
                self.grid[self.r][self.c:] = [" "] * (self.cols - self.c)
                for k in range(self.r + 1, self.rows):
                    self.grid[k] = [" "] * self.cols
        elif final == "K":
            self.grid[self.r][self.c:] = [" "] * (self.cols - self.c)

    def lines(self):
        out = ["".join(ch for ch in row).rstrip() for row in self.grid]
        while out and not out[-1]:
            out.pop()
        return out

    def text(self):
        return "\n".join(self.lines())


class Session:
    def __init__(self, argv, env=None, rows=24, cols=80, cwd=None):
        self.term = Term(rows, cols)
        self.raw = b""
        pid, fd = pty.fork()
        if pid == 0:
            if cwd:
                os.chdir(cwd)
            e = dict(os.environ)
            e.update(env or {})
            e["TERM"] = "xterm"
            os.execvpe(argv[0], argv, e)
        self.pid, self.fd = pid, fd
        fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.settle()

    def settle(self, idle=None, limit=5.0):
        """Read until the program has gone quiet: `idle` seconds after its last output
        (PTY_IDLE in the environment scales it for slow builds), or up to 0.4 s if it
        has not answered at all yet."""
        idle = idle or float(os.environ.get("PTY_IDLE", "0.05"))
        start = time.time()
        end = start + limit
        last = None
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.01)
            if r:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return False
                if not data:
                    return False
                self.raw += data
                self.term.feed(data)
                last = time.time()
            elif last is not None and time.time() - last >= idle:
                break
            elif last is None and time.time() - start >= 0.4:
                break
        return True

    def send(self, data, settle=True):
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)
        if settle:
            self.settle()

    def type(self, text):
        """Type text a key at a time, as a person would."""
        for ch in text.encode().decode("utf-8"):
            os.write(self.fd, ch.encode())
            time.sleep(0.004)
        self.settle()

    def screen(self):
        return self.term.lines()

    def alive(self):
        try:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
            return pid == 0
        except ChildProcessError:
            return False

    def close(self):
        try:
            os.kill(self.pid, 9)
            os.waitpid(self.pid, 0)
        except (ProcessLookupError, ChildProcessError):
            pass
        os.close(self.fd)
