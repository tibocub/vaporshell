# Configuration

The standalone (POSIX) build is configured with a Kconfig menu, the same system
Linux and NuttX use. Nothing to install: [Kconfiglib](tools/kconfig/README.md) is
vendored, so `python3` is enough.

```sh
make menuconfig            # curses menu (make guiconfig: a Tk window)
make                       # builds what .config says
make size-report           # what the current configuration costs, object by object
```

No `.config` means the defaults of `config/Kconfig`, which are the full shell.
`.config` is yours: it is not tracked. `make distclean` removes it.

## Presets

| Command | What you get |
|---|---|
| `make full_defconfig` (or `defconfig`) | everything, tuned for speed. The default |
| `make tiny_defconfig` | a small but comfortable shell: bash and POSIX modes, the line editor, a few light quality-of-life features |
| `make tinyest_defconfig` | the smallest binary: script-only, POSIX-only |

`make savedefconfig` writes `./defconfig`, the shortest file that reproduces your
`.config`; copy it to `config/` to make a preset of your own.

## The "Language" and "Interactive shell" menus

| Option | Default | Effect when off |
|---|---|---|
| POSIX mode only | off | no bash extensions (arrays, `[[ ]]`, process substitution, ...): about 190 KB less |
| Line editor | on | the shell reads plain lines (no `bind`, no `~/.inputrc`); about 100 KB less with history and completion |
| History | on | no `history`, `fc` or `!!`; the editor has no Up/Down history |
| Tab completion | on | Tab inserts nothing |
| Programmable completion | on | `complete`, `compgen`, `compopt` are accepted and do nothing (rc files keep working) |
| fish-style specs | on | `complete -c ... -s -l -d` is not understood |
| Syntax highlighting, autosuggestions, terminal title | on | each is a few KB |

A bash builtin that a build leaves out still exists as a stub when the build has bash
mode, so an rc file that calls it does not break.

The tests follow the configuration: a build without a feature skips the tests that need
it (`tests/config-needs.txt`, `NEEDS` in `tests/pty/run.py`) and says how many it skipped.

## The "Build" menu

| Option | Default | Effect |
|---|---|---|
| Optimization: speed / size | speed (`-O2`) | `-Os` is ~22% smaller |
| `--gc-sections` | on | the linker drops functions nothing uses |
| Link-time optimization | off | a few percent, slower build |
| Strip symbols | off | ~12% smaller; no function names in crash reports |
| Debug information | off | `-g` |
| Security hardening | on | stack protector, PIE, RELRO, fortify (docs/security.md): ~3% of the binary on the full shell, ~6% on a stripped size build |

Measured with gcc on x86-64 (full shell, default flags 403 KB):

| Config | Binary |
|---|---|
| none / `full_defconfig` | 403 KB |
| `tiny_defconfig` | 284 KB |
| `tinyest_defconfig` | 164 KB |

## Notes

- The sanitizer (`make asan`), fuzz and `strict` builds ignore `.config` on purpose
  (`NOCONFIG=1`): they always test everything.
- On NuttX the vaporOS build has its own Kconfig (`VAPOROS_VAPORSHELL_*`); it does not use
  these switches yet.
- `make HARDEN=0` and `make CFLAGS=...` on the command line still win over `.config`.
