# An empty parameter is a real argument under "$@" and disappears under $@.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- ""
show "$@"
show $@
set -- a "" b
show "$@"
show $@
set -- "" ""
show "$@"
