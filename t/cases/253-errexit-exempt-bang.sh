# `!` tests its operand, and the result of the `!` is exempt as well: POSIX
# exempts the whole pipeline beginning with `!`, so `! true` leaves status 1
# behind and the script keeps going.
set -e
! false
echo "negated-failure status=$?"
! true
echo "negated-success status=$?"
! false | true
echo "negated-pipeline status=$?"
if ! false; then echo in-if; fi
echo reached
