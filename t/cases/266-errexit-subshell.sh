# `set -e` is inherited by a subshell, so the subshell stops at its own first
# failure -- `in-subshell` is never printed. The subshell is then itself a
# failing command in the parent, which ends the script.
set -e
echo before
(false; echo in-subshell)
echo not-reached
