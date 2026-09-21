# ${var/pat/rep}: every form and anchor, quoting, overlap, extglob, arrays, and a large value.
# A large value used to take seconds (each start position tried every end, copying and matching each time);
# a plain-text pattern is now compared directly, and one without * or ( can only match so many bytes.
# rep first
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/hello/X}]"
)
# rep all
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//hello/X}]"
)
# rep all single
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//o/0}]"
)
# rep anchor start
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/#hello/X}]"
)
# rep anchor start miss
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/#world/X}]"
)
# rep anchor end
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/%hello/X}]"
)
# rep anchor end miss
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/%world/X}]"
)
# rep delete
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//l/}]"
)
# rep no rep
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//hello}]"
)
# rep overlap
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${t//aa/X}]"
)
# rep overlap2
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${t//aaa/X}]"
)
# rep q mark
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//?o/X}]"
)
# rep q marks
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//l??/X}]"
)
# rep bracket
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//[lo]/X}]"
)
# rep bracket neg
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//[!lo ]/X}]"
)
# rep class
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//[[:space:]]/_}]"
)
# rep star
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//w*d/X}]"
)
# rep star all
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//l*o/X}]"
)
# rep star anchor end
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/%o*/X}]"
)
# rep star anchor start
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/#*o/X}]"
)
# rep empty anchor start
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/#/X}]"
)
# rep empty anchor end
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/%/X}]"
)
# rep empty pattern
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//""/X}]"
)
# rep empty pattern first
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/""/X}]"
)
# rep amp
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//o/[&]}]"
)
# rep amp quoted
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//o/"[&]"}]"
)
# rep amp escaped
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//o/[\&]}]"
)
# rep quoted star
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${u//"*"/X}]"
)
# rep escaped star
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${u//\*/X}]"
)
# rep quoted q
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${u//"?"/X}]"
)
# rep escaped q
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${u//\?/X}]"
)
# rep quoted bracket
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${u//"["/X}]"
)
# rep backslash
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${b//\\/X}]"
)
# rep paren literal
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${p//"("/X}]"
)
# rep pipe literal
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${p//|/X}]"
)
# rep extglob plus
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${t//+(a)/X}]"
)
# rep extglob alt
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//@(hello|world)/X}]"
)
# rep extglob not
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//!(l)/X}]"
)
# rep multibyte lit
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//é/E}]"
)
# rep bound star mb
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${m//é*é/X}]"
)
# rep pattern longer than value
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//hello world hello and more/X}]"
)
# rep pattern equals value
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s/hello world hello/X}]"
)
# rep value empty
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${e//a/X} ${e/#/X} ${e/%/X}]"
)
# rep single char value
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${c//a/X} ${c/#a/X} ${c/%a/X} ${c//?/X}]"
)
# rep hash in pattern
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${h//#/X}]"
)
# rep percent in pattern
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${h//%/X}]"
)
# rep slash in pattern
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${sl//\//X}]"
)
# rep many matches
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${r//ab/X}]"
)
# rep adjacent
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${t//a/aa}]"
)
# rep rep longer
( s="hello world hello"; t=aaaaa; u="a*b?c[d"; b='a\b\c'; p="a(b)|c"; m="éaéaé日本語é"; e=""; c=a; h="a#b%c"; sl="a/b/c"; r=ababababab; echo "[${s//l/LLLL}]"
)
# rep array
( a=(one two three tone); echo "${a[@]//o/0}"; echo "${a[@]/#t/T}"; echo "${a[@]/%e/E}"
)
# rep positional
( set -- foo boo zoo; echo "${@//o/0}"; echo "${*/oo/OO}"
)
# rep assoc
( declare -A m=([k]=hello); echo "${m[k]//l/L}"
)
# rep nested var
( p=l; s=hello; echo "${s//$p/L}"; q="?"; echo "${s//$q/X}"; echo "${s//"$q"/X}"
)
# rep with newline
( s=$'a\nb\nc'; t=${s//$'\n'/,}; echo "$t"
)
# rep large
( s=$(printf "%*s" 3000 ""); t=${s// /ab}; echo ${#t}; u=${t//ab/}; echo ${#u}; v=${s//?/x}; echo ${#v}; w=${s/%?/Z}; echo "${w: -3}"
)
# rep large star
( s=$(printf "%*s" 600 ""); t=${s//*/X}; echo "$t"; u=${s/#*/Y}; echo "$u"
)
# rep large no match
( s=$(printf "%*s" 3000 ""); t=${s//zz/X}; echo ${#t}; t=${s//[a-z]/X}; echo ${#t}; t=${s//z?z/X}; echo ${#t}
)
# rep large literal
( s=$(printf "%*s" 20000 ""); t=${s// /ab}; echo ${#t}; u=${t//ab/}; echo ${#u}; v=${s/%?/Z}; echo "${v: -3}"
)
