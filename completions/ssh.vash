# ssh, scp, sftp: host names from ~/.ssh/config and ~/.ssh/known_hosts. Optional.
_ssh_hosts() {
  local h
  h=$(sed -n 's/^[Hh]ost[[:space:]]\{1,\}//p' ~/.ssh/config 2>/dev/null | tr ' ' '\n' | grep -v '[*?]')
  h="$h $(cut -d' ' -f1 ~/.ssh/known_hosts 2>/dev/null | tr ',' '\n' | grep -v '^[|[]')"
  COMPREPLY=($(compgen -W "$h" -- "${COMP_WORDS[COMP_CWORD]}"))
}
complete -F _ssh_hosts -o default ssh scp sftp
