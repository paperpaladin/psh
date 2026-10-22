# A redirection that cannot be opened makes the command fail, and errexit
# treats that like any other failure.
set -e
echo before
cat < no-such-file-psh-errexit
echo not-reached
