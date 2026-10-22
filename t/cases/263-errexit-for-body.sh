# Same for a `for` body: one iteration, then the script ends.
set -e
for i in a b c; do
	echo "item $i"
	false
done
echo not-reached
