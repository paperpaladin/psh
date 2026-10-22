# An assignment-only command takes its status from the command substitution in
# it, which makes `x=$(false)` a failing command. With a command word in front
# the command's own status wins, so `v=$(false) true` succeeds, and so does
# `echo $(false)`.
set -e
echo "[$(true)]"
v=$(false) true
echo "prefix ok, status=$?"
echo $(false)
echo "argument ok, status=$?"
x=$(false)
echo not-reached
