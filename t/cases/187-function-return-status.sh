f() { return 3; }
f; echo "s=$?"
g() { false; return; }
g; echo "s=$?"
h() { true; false; }
h; echo "s=$?"
