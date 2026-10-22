# A `while` condition is tested every iteration, so a failure there ends the
# loop rather than the script, and a loop that never runs its body is
# successful. The body is not exempt; the next case covers that.
set -e
while false; do echo body; done
echo "zero-iterations status=$?"
n=0
while [ "$n" -lt 2 ]; do n=$(expr "$n" + 1); echo "iter $n"; done
echo "after status=$?"
f() { false; echo in-function; }
while f; do echo body; break; done
echo reached
