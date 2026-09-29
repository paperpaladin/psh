# A function has its own parameters, so shift inside one does not touch the
# caller's.
f() {
	echo "in: $# [$1]"
	shift
	echo "after: $# [$1]"
}
set -- A B C
f x "y z" w
echo "out: $# [$1] [$2] [$3]"
