# A `while` body is not exempt either, so a failing command in it ends the
# script on the first iteration. The loop counts to three rather than being
# `while true`, so that a psh whose errexit is broken disagrees with dash and
# stops, instead of looping forever and hanging the suite. The first version
# was `while true` and the first mutation of the sweep hung on it.
set -e
n=0
while [ "$n" -lt 3 ]; do
	n=$(expr "$n" + 1)
	echo "iter $n"
	false
	echo not-reached
done
echo also-not-reached
