# Every operand of && except the last is tested. A list that short-circuits
# never reaches its last operand, so `false && true` carries on with status 1
# and only a failure in the operand that actually runs last ends the script.
set -e
false && echo not-run
echo "short-circuit status=$?"
false && false
echo reached-twice
true && true
echo "both-ran status=$?"
false && true && echo not-run
echo reached-thrice
