# With no parameters, the quotes written around "$@" vanish with it, but
# quotes written anywhere else in the same word do not. dash passes zero
# arguments for "$@" and one empty argument for ""$@.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set --
show "$@"
show ""$@
show ""$@""
show a"$@"
show "$@"x
show "x$@y"
show "$@""$@"
show "${@}"
show a"${@}"
set -- ""
show "$@"
show a"$@"
