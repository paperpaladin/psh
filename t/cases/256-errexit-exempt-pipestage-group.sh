# A pipeline stage runs in its own process and `set -e` is live there, so a
# failing command inside a group used as a stage ends that stage: `in-group`
# never reaches cat. In a tested position the stage inherits the exemption and
# the same group prints it.
set -e
{ false; echo in-group; } | cat
echo "stage status=$?"
if { false; echo in-tested-group; } | cat; then echo t; else echo e; fi
echo reached
