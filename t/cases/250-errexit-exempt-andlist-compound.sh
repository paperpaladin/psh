# The same exemption reaches inside a compound or a function used as a
# non-final operand of &&.
set -e
{ false; echo in-group; } && echo group-then
f() { false; echo in-function; }
f && echo function-then
(false; echo in-subshell) && echo subshell-then
echo reached
