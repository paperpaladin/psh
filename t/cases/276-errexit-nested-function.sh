# The exemption is inherited all the way down: a function called from a
# function called from a tested position has it, and so does a subshell inside
# that. Once the outermost call returns, the next command is checked again.
set -e
g() { false; echo in-g; (false; echo in-g-subshell); echo after-g-subshell; }
f() { g; echo in-f; }
if f; then echo t; else echo e; fi
f || echo handled
echo before
f
echo not-reached
