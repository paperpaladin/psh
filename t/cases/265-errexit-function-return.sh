# A function that returns non-zero is a failing command. In a tested position
# that is exempt like anything else; outside one it ends the script, and the
# script's status is what `return` was given.
set -e
f() { return 4; }
if f; then echo t; else echo "else status=$?"; fi
f || echo handled
echo before
f
echo not-reached
