# The ${...} operators work on positional parameters too, so the name scan
# has to accept digits.
set -- a "" c
echo "[${1:-d}] [${2:-d}] [${2-d}] [${4:-d}]"
echo "[${1:+y}] [${2:+y}] [${4:+y}]"
echo "[${#1}] [${#3}] [${#4}]"
echo "[${9:-ninth}] [${10:-tenth}]"
