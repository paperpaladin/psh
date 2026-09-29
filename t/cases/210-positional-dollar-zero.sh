# $0 is the script path exactly as it was given on the command line, which is
# why the oracle can compare it at all: t/run.sh hands both shells the same
# path. `set --` and `shift` must not touch it.
echo "[$0]"
echo "[${0}]"
set -- x y
shift
echo "[$0] n=$#"
