# `set +e` turns it back off, and `set -e` with no operands does not disturb
# the positional parameters on the way.
set -- a b c
set -e
echo "params still $#"
set +e
false
echo "off again, status=$?"
set -e
echo "params still $#"
false
echo not-reached
