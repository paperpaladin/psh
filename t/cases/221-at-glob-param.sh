# A glob character inside a parameter. Quoted "$@" must not glob it; the
# unquoted forms must. The files exist, so the two answers differ.
: > ax.txt
: > ay.txt
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a*.txt" "no*match" "b c"
show "$@"
show $@
show "$*"
show $*
