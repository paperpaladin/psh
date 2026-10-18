# An empty IFS turns field splitting off, so "$*" concatenates with nothing
# between. "$@" still gives one field per parameter.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" c ""
IFS=
show "$@"
show "$*"
show $@
show $*
