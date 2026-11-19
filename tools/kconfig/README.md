# Vendored Kconfiglib

[Kconfiglib](https://github.com/ulfalizer/Kconfiglib) 14.1.0 (ISC license, see
`LICENSE.txt`), with one local patch (menuconfig.py and guiconfig.py no longer print "(NEW)" after
symbols missing from `.config`), so `make menuconfig` needs nothing but `python3`.

Source: the PyPI sdist `kconfiglib-14.1.0.tar.gz`,
sha256 `bed2cc2216f538eca4255a83a4588d8823563cdd50114f86cf1a2674e602c93c`.
Only the scripts the build uses were copied. Update by replacing the files from a
newer sdist and updating this note.
