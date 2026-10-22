# Every operand of || except the last is tested, which is what makes
# `cmd || handler` the portable way to catch a failure under `set -e`.
set -e
false || echo handler-ran
echo "status=$?"
false || true
echo reached-twice
f() { false; echo in-function; }
f || echo function-handler
(exit 3) || echo subshell-handler
echo reached
