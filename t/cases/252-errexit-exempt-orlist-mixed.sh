# && and || chain left to right, so in `false || true && false` the only
# operand whose status is not tested is the final one.
set -e
false || true && echo last-ran
echo "status=$?"
true && false || echo middle-handled
echo reached
false || false || echo second-handler
echo done
