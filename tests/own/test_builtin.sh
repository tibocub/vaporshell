test 1 -eq 1 && echo eq-ok
test 1 -lt 2 && echo lt-ok
test abc = abc && echo streq-ok
test abc != xyz && echo strne-ok
test ! -f /nonexistent && echo not-ok
