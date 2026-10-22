# `until` tests its condition the same way; here the failing command is the
# one that keeps the loop going rather than the one that stops it.
set -e
until true; do echo body; done
echo "zero-iterations status=$?"
n=0
until [ "$n" -ge 2 ]; do n=$(expr "$n" + 1); echo "iter $n"; done
echo "after status=$?"
until { false; echo in-group; }; do echo body; break; done
echo reached
