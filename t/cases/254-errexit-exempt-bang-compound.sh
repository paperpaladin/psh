# The `!` exemption reaches into a group, a function and a loop condition.
set -e
! { false; echo in-group; }
echo "group status=$?"
f() { false; echo in-function; }
! f
echo "function status=$?"
while ! { false; echo in-cond; }; do echo body; break; done
echo reached
