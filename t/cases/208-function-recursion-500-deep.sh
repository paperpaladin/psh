# 204 recurses forever, so it trips whatever cap exists and proves only that
# one exists. dash's cap is 1000 and psh matches that number on purpose: a
# script that recurses 500 deep works in dash and has to work here. Nothing
# checked it, and MAXFUNCDEPTH=50 passed the whole corpus.
#
# The countdown goes through `expr` because psh has none of the three obvious
# ways to do it: no positional parameters, no $(( )), and no ${n#x}. Function
# globals are shared with the caller, which 191-function-globals.sh pins.
n=500
f() {
	if [ "$n" -gt 0 ]; then
		n=$(expr "$n" - 1)
		f
	else
		echo bottom
	fi
}
echo before
f
echo "after n=$n"
