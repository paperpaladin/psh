# No parameters at all. "$@" expands to nothing -- not to one empty word --
# so the command runs with zero arguments, while "" gives it one. This is the
# case that separates a real "$@" from one wired to the empty string.
set --
echo "n=$#"
show() { echo "count=$#"; }
show "$@"
show ""
show "$*"
for i in "$@"; do echo "iterated"; done
echo done
