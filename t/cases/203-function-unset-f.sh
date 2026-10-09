f() { echo gone; }
unset -f f
unset -f nosuchfn
echo "s=$?"
