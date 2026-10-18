# $@ inside the word of ${V:-...} and ${V:+...}. The sub-expansion is copied
# back into the outer one, and the copy used to flatten the field marks, so
# two parameters came out as one argument.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c d"
show ${u:-"$@"}
show ${u:-$@}
show "${u:-$@}"
show "${u:-$*}"
v=set
show "${v:+$@}"
show ${v:+$@}
set --
show "${u:-$@}"
