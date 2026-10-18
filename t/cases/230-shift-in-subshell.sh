# A subshell gets a copy, so a shift inside one is invisible outside.
set -- a b c
( shift 2; echo "sub: $# [$1]" )
echo "outer: $# [$1]"
