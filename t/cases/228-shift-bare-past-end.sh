# The same with no count: a bare shift with no parameters left also ends the
# script rather than returning non-zero.
set -- a
shift
echo "n=$#"
shift
echo "this line must not appear"
