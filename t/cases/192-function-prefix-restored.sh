# An assignment prefix is visible inside and gone afterwards, exactly as
# it is for a regular command. dash does this; POSIX leaves it unspecified.
f() { echo "in=$v"; }
v=1 f
echo "after=$v"
