# `elif` has the same exemption as `if`, and so does the condition of a second
# elif that is only reached because the first one failed.
set -e
if false; then
	echo if-taken
elif false; then
	echo elif1-taken
elif false; then
	echo elif2-taken
else
	echo else-taken
fi
echo reached
if false; then :; elif false; then :; fi
echo "status=$?"
