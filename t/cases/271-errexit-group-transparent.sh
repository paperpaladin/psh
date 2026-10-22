# A brace group hands back the status of the list inside it and is not itself
# a command that errexit checks, so a group whose last command was exempt does
# not end the script. A subshell is a command, so the same list inside ( )
# does end it.
set -e
{ false && true; }
echo "group status=$?"
{ ! true; }
echo "negation in group status=$?"
{ false || true; }
echo "or in group status=$?"
(false && true)
echo not-reached
