# IFS does not apply to "$@": the fields are the parameters whatever IFS is.
# It does apply to "$*", which joins with the first byte of it.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c:d" "" e
IFS=:
show "$@"
show "$*"
show $@
show $*
show "${@}"
show "${*}"
show ${@}
show ${*}
