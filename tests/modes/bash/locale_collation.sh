# Glob results and [[ a < b ]] sort by the collation locale, and follow LC_ALL
# being assigned, unset, scoped to one command, or set inside a subshell.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch
: > a.txt; : > B.txt; : > c.txt; : > Z.txt; : > .dot; mkdir d
LC_ALL=C; echo *; [[ a < B ]] || echo C-order; [[ B < a ]] && echo B-before-a
( LC_ALL=C; echo * ); LC_ALL=C echo *
unset LC_ALL; echo *; [[ a < B ]] && echo a-before-B-or-not
shopt -s dotglob; LC_COLLATE=C; echo *; shopt -u dotglob
LC_ALL=C; echo *; LC_ALL=no_such_locale_zz; echo *
