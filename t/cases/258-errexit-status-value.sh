# The shell exits with the failing command's status, not with 1.
set -e
echo before
(exit 3)
echo not-reached
