echo a > f1 > f2
cat f2
test ! -s f1 && echo f1empty
