# `set -- "$@"` must be a no-op, which it is only if the expansion is read
# before the vector is replaced. Whitespace in a parameter is what makes a
# botched version visible.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "" "c	d"
set -- "$@"
show "$@"
set -- "$@" "e f"
show "$@"
set -- $@
show "$@"
