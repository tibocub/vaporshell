# complete -p prints in bash's fixed order, string arguments quoted
complete -u -j -F _x -o nospace -o default -A shopt -W 'a b' -X '!*.c' -P pp -S ss -G '*.c' -C cmd foo
complete -p foo
complete -W a -o filenames -o bashdefault z
complete -p z
complete -F f -D; complete -p -D
