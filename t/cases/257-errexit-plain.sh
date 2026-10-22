# The ordinary case: with `set -e` a failing command ends the script, and the
# script's status is that command's status. Nothing after it runs, not even
# the rest of the same list.
set -e
echo before
false
echo not-reached; echo also-not-reached
