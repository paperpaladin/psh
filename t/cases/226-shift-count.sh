# shift N, including N=0 and N equal to $#.
set -- a b c d e
shift 2
echo "$# $1"
shift 0
echo "$# $1"
shift 3
echo "$# [$1]"
echo "status=$?"
