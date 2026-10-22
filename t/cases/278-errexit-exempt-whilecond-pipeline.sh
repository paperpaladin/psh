# A `while` condition that is a pipeline and one that is an AND-OR list. The
# exemption covers the whole condition, so a failing stage or a failing
# left-hand operand in there is not a failing command.
set -e
while false | cat; do echo body; break; done
echo "pipeline-condition status=$?"
while false && true; do echo body; break; done
echo "and-condition status=$?"
n=0
while [ "$n" -lt 1 ] && true; do n=$(expr "$n" + 1); echo "iter $n"; done
echo reached
