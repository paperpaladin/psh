# A command that cannot be found fails with 127, and that is the status the
# shell exits with.
set -e
echo before
no-such-command-psh-errexit
echo not-reached
