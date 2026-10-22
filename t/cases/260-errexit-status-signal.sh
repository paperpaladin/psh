# A child killed by a signal reports 128+signo, and errexit passes that on.
set -e
echo before
sh -c 'kill -9 $$'
echo not-reached
