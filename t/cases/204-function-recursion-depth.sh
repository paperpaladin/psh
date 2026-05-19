# dash refuses past 1000 frames and exits 2; so does this.
f() { f; }
echo before
f
echo unreached
