f() { echo one; echo two; }
f | wc -l
g() { cat; }
echo feed | g
