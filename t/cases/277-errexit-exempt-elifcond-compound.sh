# A second shape for the elif exemption: the elif condition is only reached
# because the `if` condition failed, and the exemption follows it into a
# group, a function and a pipeline.
set -e
f() { false; echo in-function; }
if false; then
	echo if-taken
elif { false; echo in-group; }; then
	echo elif-group-taken
elif f; then
	echo elif-function-taken
elif false | true; then
	echo elif-pipeline-taken
else
	echo else-taken
fi
echo reached
