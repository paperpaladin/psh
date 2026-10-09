f() { for i in 1 2; do echo $i; break; done; echo after-loop; }
f
echo "s=$?"
