# git: subcommands, then branches/tags for the ones that take a ref. Optional:
# copy to ~/.config/vaporshell/completions/ (or /etc/vaporshell/completions/).
complete -c git -n '[[ $COMP_CWORD -eq 1 ]]' -f -a 'add branch checkout clone commit diff fetch init log merge pull push rebase reset restore show stash status switch tag'
_git_refs() {
  local sub=${COMP_WORDS[1]}
  case $sub in
    checkout|switch|merge|rebase|branch|diff|log|show|reset|restore)
      COMPREPLY=($(compgen -W "$(git for-each-ref --format='%(refname:short)' refs/heads refs/tags 2>/dev/null)" -- "${COMP_WORDS[COMP_CWORD]}"))
      ;;
  esac
}
complete -F _git_refs -o default git
