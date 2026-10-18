# Shifting further than $# is not a status-1 failure. shift is a POSIX
# special builtin, so dash prints a diagnostic and the script STOPS: the
# echo below never runs and the exit status is 2.
set -- a b
shift 3
echo "this line must not appear"
