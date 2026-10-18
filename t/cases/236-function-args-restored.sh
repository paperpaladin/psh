# A call replaces the parameters and a return puts them back, at every depth.
outer() {
	echo "outer in: $# [$1]"
	inner p "q r"
	echo "outer back: $# [$1]"
}
inner() {
	echo "inner: $# [$1] [$2]"
	shift
	echo "inner shifted: $# [$1]"
}
set -- top1 top2
outer o1 o2 o3
echo "top: $# [$1] [$2]"
