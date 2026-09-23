# compgen: generate a word list from a script, the same way bash's tab completion would build one
# internally -- unlike complete/compopt/bind, this is useful outside interactive completion.
rm -rf vs-scratch; mkdir vs-scratch && cd vs-scratch    # own directory: the runner's cwd is not empty on every platform
touch afile1 afile2 bfile1
# wordlist prefix
( compgen -W "apple banana avocado" -- a
)
# wordlist no prefix
( compgen -W "one two three"
)
# wordlist no match
( compgen -W "apple banana" -- xyz; echo rc=$?
)
# files prefix (both must appear; the order readdir returns them in is not
# portable -- and here, since the reference bash and vaporshell each read a
# different filesystem, not even repeatable across the two)
( out="$(compgen -f -- a)"
  case $out in *afile1*) ;; *) echo "missing afile1";; esac
  case $out in *afile2*) ;; *) echo "missing afile2";; esac
  echo done
)
# variable action
( XYZ_TEST=1; compgen -A variable -- XYZ_
)
# function action
( foo_func() { :; }; compgen -A function -- foo
)
# builtin action
( compgen -A builtin -- ech
)
# alias action
( alias xyz='ls'; compgen -A alias -- xy
)
# keyword action
( compgen -A keyword -- wh
)
# exclude glob
( compgen -X "a*" -W "apple avocado banana" -- 
)
# prefix suffix
( compgen -P "pre_" -S "_suf" -W "a b" -- a
)
# unknown action errors
( compgen -A nosuchaction 2>/dev/null; echo rc=$?
)
# v shorthand for variable
( ZZZ_ABC=1; compgen -v -- ZZZ_
)

# builtin shorthand -b
( found=0; while IFS= read -r l; do [ "$l" = echo ] && found=1; done <<< "$(compgen -b)"; [ $found = 1 ] && echo ok
)

# alias shorthand -a
( alias cgshort=ls; compgen -a
)

# keyword shorthand -k
( compgen -k -- wh
)
