# ;& and ;;& on a line of their own (bash_completion writes them that way)
case i in
    i) echo a
    ;&
    j) echo b
    ;;&
    i) echo c
    ;;
    *) echo not-here
    ;;
esac
case x in
    [xy])
        if true; then echo one; fi
        ;;&
    x) echo two ;;
esac
