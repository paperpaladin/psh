f() { for i in 1 2 3; do echo $i; return 7; done; echo unreached; }
f; echo "s=$?"
g() { while :; do for j in a b; do return 4; done; done; echo unreached; }
g; echo "s=$?"
