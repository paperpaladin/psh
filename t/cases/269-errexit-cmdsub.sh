# `set -e` crosses into a command substitution, and the exemption does not:
# dash kills the substitution's failing command even when the command that
# contains it is being tested, so the substitution comes back empty rather
# than carrying the text that follows the failure. `set +e` inside the
# substitution is how a script asks for the other behaviour.
set -e
if [ -n "$(false; echo in-sub)" ]; then echo non-empty; else echo empty; fi
echo "[$(set +e; false; echo in-sub)]"
for w in $(false; echo a b); do echo "item $w"; done
echo "loop ran zero times, status=$?"
x=$(false; echo in-sub) || echo substitution-failed
echo "[$x]"
echo reached
