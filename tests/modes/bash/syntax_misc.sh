# function keyword, case fall-through, here-strings, time, ((, [[ as words.
function f { echo hi; }; f; function g() { echo g; }; g; function h-1 { echo h; }; h-1
function args { echo "$1-$#"; }; args a b
case a in a) echo 1;& b) echo 2;& c) echo 3;; d) echo 4;; esac
case abc in a*) echo 1;;& *b*) echo 2;;& *z) echo no;; *c) echo 3;; esac
case x in x) echo x;; y) echo y;; esac
cat <<< "hello"; read a b <<< "x y"; echo "$a|$b"; v=abc; cat <<< $v
x=1; cat <<< "v=$x $((1+1)) $(echo cmd)"; cat <<< "a  b"
TIMEFORMAT=X; time true
TIMEFORMAT='%R'; time false 2>/dev/null; echo "rc=$?"
