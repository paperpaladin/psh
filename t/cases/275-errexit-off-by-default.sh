# The control for every case above: with the option off, which is the default,
# a failing command changes $? and nothing else.
echo before
false
echo "status=$?"
f() { false; echo in-function; }
f
(false; echo in-subshell)
echo "end status=$?"
