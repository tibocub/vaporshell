#!/bin/bash
# A small bashttpd-style handler: one request on stdin, one response on stdout.
# Written to use what real bash servers use: [[ =~ ]] with BASH_REMATCH, assoc
# arrays, read -r -t, printf -v, ${var//}, case, here-strings, process
# substitution, local, and printf '%s\r\n'.
shopt -s extglob
declare -A HDR
ROOT=${DOCROOT:-.}
respond() { # status text type body
    local len=${#4}
    printf 'HTTP/1.0 %s %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s' "$1" "$2" "$3" "$len" "$4"
}
urldecode() { local s=${1//+/ }; printf '%b' "${s//%/\\x}"; }
read -r -t 5 line || exit 1
line=${line%$'\r'}
if [[ $line =~ ^(GET|HEAD|POST)\ ([^ ?]+)(\?([^ ]*))?\ HTTP/([0-9.]+)$ ]]; then
    method=${BASH_REMATCH[1]} path=$(urldecode "${BASH_REMATCH[2]}") query=${BASH_REMATCH[4]}
else
    respond 400 "Bad Request" text/plain "bad request"; exit 0
fi
while read -r -t 5 h && [[ $h != $'\r' && -n $h ]]; do
    h=${h%$'\r'}; HDR[${h%%:*}]=${h#*: }
done
if [[ $method == POST && -n ${HDR[Content-Length]} ]]; then
    read -r -N "${HDR[Content-Length]}" body
fi
declare -A Q
IFS='&' read -ra kv <<<"$query"
for p in "${kv[@]}"; do Q[${p%%=*}]=$(urldecode "${p#*=}"); done
case $path in
    /) respond 200 OK text/html "<h1>index</h1><p>host=${HDR[Host]:-none}</p>" ;;
    /echo) printf -v out '%s|' "${!Q[@]}"; respond 200 OK text/plain "keys=$(tr '|' '\n' <<<"${out%|}" | sort | paste -sd,) name=${Q[name]} body=$body" ;;
    /headers) respond 200 OK text/plain "$(for k in "${!HDR[@]}"; do echo "$k=${HDR[$k]}"; done | sort)" ;;
    /file/*)
        f=$ROOT/${path#/file/}
        if [[ $f == *..* ]]; then respond 403 Forbidden text/plain forbidden
        elif [[ -f $f ]]; then
            case ${f##*.} in html) t=text/html;; txt) t=text/plain;; *) t=application/octet-stream;; esac
            respond 200 OK $t "$(<"$f")"
        else respond 404 "Not Found" text/plain "no $path"; fi ;;
    *) respond 404 "Not Found" text/plain "no $path" ;;
esac
