# shift drops $1 and renumbers, and $# follows it down.
set -- a b c d
echo "$# $1 $2"
shift
echo "$# $1 $2"
shift
echo "$# $1"
echo "status=$?"
