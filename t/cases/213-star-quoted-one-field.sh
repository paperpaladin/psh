# "$*" is one field, the parameters joined with the first byte of IFS. The
# only way to tell it from "$@" is a parameter containing whitespace.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c	d" "" e "f g h"
show "$*"
