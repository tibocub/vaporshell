# shopt: listing, glob options, nocasematch, extglob, globstar.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
shopt nullglob; echo "rc=$?"; shopt -s nullglob; shopt nullglob; echo "rc=$?"; shopt -q nullglob; echo "q=$?"; shopt -u nullglob
shopt -s nullglob dotglob; shopt nullglob dotglob; shopt -u nullglob dotglob; shopt -q nullglob dotglob; echo "rc=$?"
shopt -s histappend cmdhist; shopt histappend; shopt -u checkwinsize; shopt checkwinsize; shopt -u lastpipe; echo "rc=$?"
shopt -p nullglob; shopt -u -p nullglob | while read a b c; do echo "$a $b $c"; done
mkdir -p d/e .h; : > a.txt; : > B.txt; : > .dot; : > d/f.txt; : > d/e/g.txt; : > d/e/H.TXT; : > .h/x
echo *.zzz; shopt -s nullglob; echo *.zzz [x]; for f in *.zzz; do echo loop; done; echo done; shopt -u nullglob
echo *; shopt -s dotglob; echo *; echo */x .h/*; shopt -u dotglob
echo b.txt B.TXT; shopt -s nocaseglob; echo b.* [a-b].txt B.TXT d/E/*.txt; shopt -u nocaseglob
shopt -s nocasematch; case ABC in abc) echo m;; esac; [[ ABC == abc ]] && echo dbl; [[ x == X* ]] && echo star
shopt -u nocasematch; [[ ABC == abc ]] || echo off
shopt -s globstar; echo **/*.txt; echo **; echo d/**/g.txt; echo **/; shopt -u globstar
echo **/*.txt
shopt -s globstar dotglob; echo **/x; shopt -u globstar dotglob
shopt -s extglob
for w in a ab abc; do [[ $w == ?(a)b* ]] && echo "$w?"; done; case ab in ?(x)ab) echo q;; esac
for w in "" a aa aaa b ab; do [[ $w == *(a) ]] && echo "[$w]*"; done; [[ ababab == *(ab) ]] && echo rep
for w in "" a aa b; do [[ $w == +(a) ]] && echo "[$w]+"; done; [[ foobar == +(foo|bar) ]] && echo alt
[[ foo == @(foo|bar) ]] && echo 1; [[ baz == @(foo|bar) ]] || echo 2; [[ foo.c == *.@(c|h) ]] && echo 3
for w in a b c ab; do [[ $w == !(a|b) ]] && echo "[$w]!"; done; [[ file.txt == !(*.c) ]] && echo notc; [[ x == !(x) ]] || echo excl
[[ a.tar.gz == *.@(tar|zip).@(gz|bz2) ]] && echo n1; [[ aXb == a@(X|Y|+(Z))b ]] && echo n2
echo !(*.txt); echo +(a|B).txt; echo @(a|B).txt
for f in x.c x.h x.o; do case $f in *.@(c|h)) echo "$f src";; *) echo "$f other";; esac; done
x=aaabbb; echo ${x##+(a)} ${x%%+(b)} ${x//+(a)/-}
shopt -u extglob
alias hi="echo aliased"
shopt -s expand_aliases
alias hi2="echo two"
hi2
x=abc; echo ${x/b/[&]}; shopt -u patsub_replacement; echo ${x/b/[&]}; shopt -s patsub_replacement; echo ${x/b/[&]}
set -e; x=$(false; echo after); echo "[$x]"; shopt -s inherit_errexit; x=$(false; echo after); echo "[$x]"
