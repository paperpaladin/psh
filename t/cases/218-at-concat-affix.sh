# A word that is more than just "$@": the text before it joins the first
# parameter and the text after joins the last, and the middle parameters are
# untouched fields of their own, empty ones included.
show() { echo "count=$#"; for a in "$@"; do echo "[$a]"; done; }
set -- "a b" "c	d" "" e
show pre"$@"post
show pre"$*"post
