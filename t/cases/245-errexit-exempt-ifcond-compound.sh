# The exemption follows the condition into a brace group, a subshell and a
# function, so a failing command inside any of them does not end the script
# and the commands after it still run.
set -e
if { false; echo in-group; }; then echo t; else echo e; fi
if (false; echo in-subshell); then echo t; else echo e; fi
f() { false; echo in-function; }
if f; then echo t; else echo e; fi
g() { (false; echo in-nested-subshell); echo in-caller; }
if g; then echo t; else echo e; fi
echo reached
