# The option is per-shell. Turned on inside a subshell it does not reach the
# parent, and turned off inside one it does not reach the parent either.
echo start
(set -e; false; echo not-reached-in-sub)
echo "parent survived, status=$?"
set -e
(set +e; false; echo in-sub-with-e-off)
echo reached
