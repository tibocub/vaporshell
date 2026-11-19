# Using vaporshell as your everyday shell

## At home (root): `chsh`

```sh
make && sudo make install
echo /usr/local/bin/vaporshell | sudo tee -a /etc/shells
chsh -s /usr/local/bin/vaporshell
```

`chsh` refuses shells not listed in `/etc/shells`, hence the second line.
Log out and back in. Vaporshell then runs as a login shell and reads
`/etc/profile` and `~/.bash_profile` / `.bash_login` / `.profile` (the usual
bash way, so make `~/.bash_profile` source `~/.bashrc` if you want it).
Keep a root terminal or `chsh -s /bin/bash` handy for the first login.

## No root: start it from `~/.bashrc`

Put this at the **top** of `~/.bashrc` (after the usual "not interactive,
return" guard is fine too):

```bash
if [[ $- == *i* && -z $VASH_EXEC && -x ~/.local/bin/vaporshell ]]; then
  export VASH_EXEC=1          # loop guard: vaporshell reads this same ~/.bashrc
  exec ~/.local/bin/vaporshell
fi
```

* Build once there and `install -D build/vaporshell ~/.local/bin/vaporshell`.
* Loop guard: `VASH_EXEC` is exported before the `exec`, so when vaporshell
  sources `~/.bashrc` the `if` is false and the rest of the file runs normally.
  It also means a `bash` you start *from* vaporshell stays bash.
* `$-` check keeps `ssh host cmd`, scp and scripts on plain bash.
* If the binary is missing you just get bash (`-x` test). Escape hatch if
  vaporshell ever breaks: `VASH_EXEC=1 bash` (or `bash --norc`).
* Here `$SHELL` stays `/bin/bash`, so tools see bash. Use `~/.bash_profile`
  for the same snippet if the root-less terminal starts login shells and
  `.bash_profile` doesn't source `.bashrc`.

## What programs see

* Inside vaporshell `BASH_VERSION`, `BASH_VERSINFO`, `$BASH`, `shopt`, etc.
  are set, which is what nearly all bash-detecting scripts (nvm, conda,
  git-prompt, completions, `/etc/profile.d/*`) test.
* `$SHELL` is inherited from the login (`/usr/local/bin/vaporshell` after
  `chsh`; `/bin/bash` in the root-less setup). Programs that look at its
  basename (some `eval "$(tool init $(basename $SHELL))"` setups) won't know
  "vaporshell". If one bites, pick one:
  * `export SHELL=/bin/bash` in `~/.vashrc` (tools think bash, but terminals/
    tmux/`$SHELL -c` spawn real bash from then on), or
  * call it explicitly: `eval "$(tool init bash)"`.
