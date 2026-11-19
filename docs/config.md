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

**Status.** Phase 1 (this) is the compiler/linker side of the menu. The two small
presets already select size-oriented flags; the *feature* cuts that make them really
small (the editor and completion, job control, bash extensions, ...) arrive with the
module switches in the next phases, and these presets will be extended then.

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
| `tiny_defconfig` (phase 1) | 298 KB |
| `tinyest_defconfig` (phase 1) | 282 KB |

## Notes

- The sanitizer (`make asan`), fuzz and `strict` builds ignore `.config` on purpose
  (`NOCONFIG=1`): they always test everything.
- On NuttX the vaporOS build has its own Kconfig (`VAPOROS_VAPORSHELL_*`); the
  feature switches will be shared with it when they exist.
- `make HARDEN=0` and `make CFLAGS=...` on the command line still win over `.config`.
