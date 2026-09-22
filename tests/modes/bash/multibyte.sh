# Characters, not bytes, in a multibyte locale: ${#v}, ${v:off:len}, ${v^^}, ${v#?}, ${v//?/x}, ? and [...] in
# patterns and globs, [[:class:]], nocasematch, declare -u, read -n. Invalid bytes count as one character each.
# In the C locale (and on NuttX, which has no locales) every one of these is bytes, as in bash there; the
# expected output comes from bash under the same locale either way.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
# length ${#s}
( s="aéb日本z"; echo ${#s}
)
# substring off
( s="aéb日本z"; echo "${s:1}" "${s:2:2}" "${s: -2}" "${s:0:3}"
)
# substring neg len
( s="aéb日本z"; echo "${s:1:-1}"
)
# case upper
( s="aéb日本z"; x=éàü; echo "${x^^}" "${x^}"; y=ÉÀÜ; echo "${y,,}" "${y,}"
)
# glob ? in case
( s="aéb日本z"; case é in ?) echo one;; *) echo other;; esac; case 日本 in ??) echo two;; *) echo other;; esac
)
# [[ == ? ]]
( s="aéb日本z"; [[ é == ? ]] && echo yes || echo no; [[ 日本 == ?? ]] && echo yes || echo no; [[ é == ?? ]] && echo yes || echo no
)
# bracket single
( s="aéb日本z"; [[ é == [é] ]] && echo yes || echo no; [[ é == [a-z] ]] && echo yes || echo no; [[ é == [^a] ]] && echo yes || echo no
)
# bracket range
( s="aéb日本z"; [[ é == [à-ü] ]] && echo yes || echo no; [[ 本 == [一-龥] ]] && echo yes || echo no
)
# bracket class
( s="aéb日本z"; [[ é == [[:alpha:]] ]] && echo alpha || echo notalpha; [[ 日 == [[:alpha:]] ]] && echo alpha || echo notalpha; [[ é == [[:lower:]] ]] && echo lower || echo notlower
)
# star then ?
( s="aéb日本z"; [[ aéb == *?b ]] && echo yes || echo no; [[ éb == *?b ]] && echo yes || echo no; [[ b == *?b ]] && echo yes || echo no
)
# replace ?
( s="aéb日本z"; echo "${s//?/X}"; echo "${s/?/X}"; echo "${s/%?/X}"; echo "${s/#?/X}"
)
# replace bracket
( s="aéb日本z"; echo "${s//[é日]/X}"
)
# trim ?
( s="aéb日本z"; echo "${s#?}" "${s##?}" "${s%?}" "${s%%?}" "${s#??}"
)
# trim star
( s="aéb日本z"; echo "${s#*é}" "${s%é*}"
)
# glob files ?
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; : > é; : > 日; : > ab; echo ? ; echo ??; echo [é]*
)
# printf %c
( s="aéb日本z"; printf "%c|" é 日本 abc; echo
)
# printf width
( s="aéb日本z"; printf "[%5s][%-5s][%.2s]\n" é 日本 aéb日
)
# read -n
( s="aéb日本z"; echo "éa日b" | { read -n 2 x; echo "[$x]"; }
)
# array elems
( s="aéb日本z"; a=(é 日 ab); echo ${#a[0]} ${#a[1]} ${#a[2]}
)
# substring in loop
( s="aéb日本z"; for ((i=0;i<${#s};i++)); do printf "%s," "${s:i:1}"; done; echo
)
# tr-like reverse
( s="aéb日本z"; r=""; for ((i=${#s}-1;i>=0;i--)); do r+="${s:i:1}"; done; echo "$r"
)
# extglob ?
( s="aéb日本z"; shopt -s extglob; [[ é == ?(é) ]] && echo yes || echo no; [[ é == @(?) ]] && echo yes || echo no; [[ é == +(?) ]] && echo yes || echo no; [[ éé == +(?) ]] && echo yes || echo no
)
# compare <
( s="aéb日本z"; [[ é > a ]] && echo gt || echo le
)
# ${!s} keys
( s="aéb日本z"; declare -A m=([é]=1 [日]=2); echo ${m[é]} ${m[日]}
)
# invalid length
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo ${#i}
)
# invalid substr
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; if [[ "${i:1:1}" == $'\xff' ]]; then echo ff; fi; if [[ "${i:3}" == $'\xc3' ]]; then echo c3; fi
)
# invalid ?
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ $i == ??? ]] && echo three || echo other; [[ $i == ???? ]] && echo four || echo other
)
# invalid replace
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; x=${i//?/X}; echo "$x"
)
# truncated tail
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo ${#i}; if [[ "${i: -1}" == $'\xc3' ]]; then echo c3; fi
)
# combining length
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo ${#c}; echo "${c:0:1}${c:2:1}"
)
# neg bracket
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ é == [^a] ]] && echo y || echo n; [[ é == [!é] ]] && echo y || echo n; [[ a == [!é] ]] && echo y || echo n
)
# mb range
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ é == [à-ÿ] ]] && echo y || echo n; [[ 日 == [あ-ん] ]] && echo y || echo n; [[ b == [à-ÿ] ]] && echo y || echo n
)
# class upper lower
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ É == [[:upper:]] ]] && echo up || echo notup; [[ é == [[:upper:]] ]] && echo up || echo notup; [[ é == [[:lower:]] ]] && echo lo || echo notlo
)
# class mix
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ é == [[:digit:]é] ]] && echo y || echo n; [[ 5 == [[:digit:]é] ]] && echo y || echo n
)
# class alnum punct
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ é == [[:alnum:]] ]] && echo y || echo n; [[ ¿ == [[:punct:]] ]] && echo y || echo n; [[ 日 == [[:alnum:]] ]] && echo y || echo n
)
# star ? mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ éa == *?a ]] && echo y || echo n; [[ aéaé == *é ]] && echo y || echo n; [[ 日本語 == 日*語 ]] && echo y || echo n; [[ 日本語 == ?本? ]] && echo y || echo n
)
# case multi
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; for w in é 日本 abc éé ""; do case $w in ?) echo "$w:1";; ??) echo "$w:2";; ???) echo "$w:3";; *) echo "$w:other";; esac; done
)
# trim mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "${s#?}" "${s##?}" "${s#??}" "${s%?}" "${s%%?}" "${s%??}" "${s#*é}" "${s##*本}" "${s%日*}" "${s%%日*}"
)
# trim brackets
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "${s#[a-z]}" "${s#[!a]}" "${s##[[:alpha:]]}" "${s%[[:alpha:]]}"
)
# replace mb anchors
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "${s/#?/X}" "${s/%?/X}" "${s/#*é/X}" "${s/%本*/X}" "${s//[éz]/Y}" "${s/日/}" "${s//日本/-}"
)
# replace amp
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "${s//?/[&]}"
)
# replace empty
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; e=""; echo "[${e//?/X}][${e/#?/X}][${e/%?/X}]"
)
# substr edges
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "${s:0:0}|${s:6}|${s:7}|${s:5:10}|${s: -1}|${s: -6:2}|${s:2:-1}|${s:1:-3}"
)
# substr array
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; a=(éa 日本 xyz); echo "${a[0]:1}" "${a[1]:1}" "${a[@]:1:1}"
)
# positional len
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; set -- éa 日本語; echo ${#1} ${#2} "${1:1}" "${2:1:1}"
)
# case conv full
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; x="éàü日ß"; echo "${x^^}" "${x^}" "${x,,}"; y="ÉÀÜ"; echo "${y,,}" "${y,}"
)
# case conv pattern
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; x=aéb; echo "${x^^[é]}" "${x^^[a-z]}" "${x^^?}" "${x,,[É]}"; y=AÉB; echo "${y,,[É]}"
)
# case attr
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; declare -u U; U=éa; echo "$U"; declare -l L; L=ÉA; echo "$L"
)
# length in arith
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo $(( ${#s} * 2 ))
)
# loop chars
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; for ((k=0;k<${#s};k++)); do printf "[%s]" "${s:k:1}"; done; echo
)
# nocasematch
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; shopt -s nocasematch; [[ É == é ]] && echo y || echo n; [[ é == [É] ]] && echo y || echo n; case É in é) echo y;; *) echo n;; esac
)
# extglob mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; shopt -s extglob; [[ éé == +(é) ]] && echo y || echo n; [[ 日本 == @(日|本)@(日|本) ]] && echo y || echo n; [[ éa == !(a)a ]] && echo y || echo n; [[ 日本語 == *(?) ]] && echo y || echo n; [[ é == ?(a|é) ]] && echo y || echo n; [[ ab == ??(é) ]] && echo y || echo n
)
# extglob replace
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; shopt -s extglob; echo "${s//+(é|日)/X}" "${s//@(a|é)/Y}"
)
# glob names
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; : > é; : > 日; : > 日本; : > ab; : > a; echo ?; echo ??; echo [é日]; echo [^a-z]*; echo ?本
)
# glob names mixed
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; : > aé; : > éa; : > bb; echo ?é; echo é?; echo *é*; echo [[:alpha:]]?
)
# printf mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; printf "%s|%c|%5s|%-3s|\n" é 日本 é 日
)
# read -n mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; echo "éa日b" | { read -n 3 x; echo "[$x]"; }; echo "日本語" | { read -n 1 y; echo "[$y]"; }
)
# read -n short
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; printf "日" | { read -n 5 x; echo "[$x] rc=$?"; }
)
# read -n delim
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; printf "é\n日" | { read -n 5 x; echo "[$x]"; }
)
# =~ mb
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ éa =~ ^.a$ ]] && echo y || echo n; [[ 日本 =~ ^..$ ]] && echo y || echo n; [[ é =~ [[:alpha:]] ]] && echo y || echo n
)
# string compare
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; [[ é > a ]] && echo gt || echo le; [[ 日 > é ]] && echo gt || echo le
)
# assoc keys
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; declare -A m=([é]=1 [日本]=2); echo ${m[é]} ${m[日本]} ${#m[@]}; t=0; for k in "${!m[@]}"; do (( t += ${#k} )); done; echo $t
)
# var names ascii
( s='aéb日本z'; i=$'a\xffb\xc3'; c=$'e\xcc\x81x'; é=1 2>/dev/null; echo rc=$?
)
# rep multibyte lit
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//é/E}]"
)
# rep multibyte q
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//?/X}]"
)
# rep multibyte qq
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//??/X}]"
)
# rep multibyte bracket
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//[éa]/X}]"
)
# rep bound 4 chars
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//????/X}]"
)
# rep bound 5 chars
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//?????/X}]"
)
# rep bound star mb
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//é*é/X}]"
)
