# `until` with a multi-command condition and with a negated one. Only the last
# command of the condition decides the loop; all of them are exempt.
set -e
until false; true; do echo body; break; done
echo "list-condition status=$?"
until ! false; do echo body; break; done
echo "negated-condition status=$?"
f() { false; echo in-function; }
until f; do echo body; break; done
echo reached
