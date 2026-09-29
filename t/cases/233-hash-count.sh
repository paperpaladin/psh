# $# and ${#} are the parameter count, and ${#} is not the length of a
# variable called '#'.
set --
echo "$# ${#}"
set -- a bb ccc
echo "$# ${#}"
shift
echo "$# ${#}"
set -- 1 2 3 4 5 6 7 8 9 10 11
echo "$# ${#}"
