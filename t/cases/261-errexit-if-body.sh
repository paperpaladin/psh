# Only the condition is exempt. The branch that runs is ordinary code, so a
# failure in it ends the script even though it sits inside an `if`.
set -e
if true; then
	echo in-then
	false
	echo not-reached
fi
echo also-not-reached
