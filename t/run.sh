#!/bin/sh
# Differential oracle. dash is the authority: for every corpus script, psh's
# stdout AND exit status must be byte-identical to dash's. stderr is not
# compared -- error wording is not something a shell subset owes anyone.
#
# Each script runs twice in the SAME scratch directory path (wiped in between)
# so that `pwd` and glob results are identical for both shells. If dash itself
# rejects a script we SKIP it rather than scoring it: the oracle cannot rule on
# something it will not run.
set -u

here=$(cd "$(dirname "$0")" && pwd)
psh=${PSH:-$here/../psh}
dash=${DASH:-/bin/dash}

[ -x "$psh" ]  || { echo "run.sh: no $psh -- run make first" >&2; exit 1; }
psh=$(cd "$(dirname "$psh")" && pwd)/$(basename "$psh")  # absolute: tests cd away
[ -x "$dash" ] || { echo "run.sh: no oracle at $dash" >&2; exit 1; }

agree=0
disagree=0
skipped=0

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT INT TERM
work=$tmp/w
out=$tmp/o
mkdir -p "$out"

# A case that needs positional parameters on its command line gets them from
# a sidecar file NAME.args, one argument per line, read verbatim: that is how
# an argument can hold a space, a tab, a glob character or no bytes at all,
# none of which survive a `set -- $(cat ...)`. Both shells get the same list.
#
# Inline rather than a function on purpose: `set --` inside a shell function
# sets that function's parameters, and they are discarded when it returns.
CANARY=209-positional-from-argv.sh

for f in "$here"/cases/*.sh; do
	name=$(basename "$f")

	set --
	if [ -f "${f%.sh}.args" ]; then
		while IFS= read -r a; do set -- "$@" "$a"; done < "${f%.sh}.args"
	fi

	rm -rf "$work"; mkdir -p "$work"
	( cd "$work" && "$dash" "$f" "$@" ) >"$out/d.out" 2>"$out/d.err"
	ds=$?
	[ "$name" = "$CANARY" ] && cp "$out/d.out" "$out/canary.out"

	# dash's own parse/feature refusal -> not a verdict we can use.
	if [ "$ds" -eq 2 ] && grep -qi 'syntax error\|unexpected' "$out/d.err"; then
		skipped=$((skipped + 1))
		echo "SKIP     $name (dash rejects it)"
		continue
	fi

	rm -rf "$work"; mkdir -p "$work"
	( cd "$work" && "$psh" "$f" "$@" ) >"$out/p.out" 2>"$out/p.err"
	ps=$?

	if [ "$ds" -eq "$ps" ] && cmp -s "$out/d.out" "$out/p.out"; then
		agree=$((agree + 1))
		[ -n "${V:-}" ] && echo "ok       $name"
	else
		disagree=$((disagree + 1))
		echo "DISAGREE $name  (dash status=$ds, psh status=$ps)"
		echo "  --- dash stdout ---"; sed -n '1,20p' "$out/d.out" | sed 's/^/  /'
		echo "  --- psh  stdout ---"; sed -n '1,20p' "$out/p.out" | sed 's/^/  /'
		if [ -s "$out/p.err" ]; then
			echo "  --- psh  stderr ---"; sed -n '1,10p' "$out/p.err" | sed 's/^/  /'
		fi
	fi
done

# --- expansions psh does not implement ------------------------------------
# These cannot live in the corpus above: the corpus requires psh and dash to
# agree, and an unimplemented expansion never will. So they are checked for
# refusing instead.
#
# ${n#x} was silently expanding to empty with no diagnostic, because the name
# scan in expand_brace stopped at ':', '-' and '+' only -- so "n#x" was taken
# for the whole variable name, found unset, and expanded to nothing while
# dash gives "xx". psh fails loudly for $((...)) for precisely this reason
# and the comment there says so. Nothing in the corpus used either form,
# which is how 207 of 207 agreement sat on top of it.
EXPANSION_REFUSALS=0
for form in 'n=xxx; echo "${n#x}"' 'n=xxx; echo "${n%x}"' \
            'echo "${n:=d}"' 'echo "${n:?w}"' \
            'set -e; false; echo reached' 'set -u; echo "$nope"' \
            'set' 'set +x'; do
	EXPANSION_REFUSALS=$((EXPANSION_REFUSALS + 1))
	echo "$form" > "$out/exp.sh"
	( cd "$work" && "$psh" "$out/exp.sh" ) >"$out/e.out" 2>"$out/e.err"
	if grep -q 'unsupported expansion\|unsupported option\|is unsupported' "$out/e.err"; then
		agree=$((agree + 1))
		[ -n "${V:-}" ] && echo "ok       refuses: $form"
	else
		disagree=$((disagree + 1))
		echo "DISAGREE $form expanded without a diagnostic"
		echo "  --- stdout ---"; sed 's/^/  /' "$out/e.out"
		echo "  --- stderr ---"; sed 's/^/  /' "$out/e.err"
	fi
done

echo "$agree agree, $disagree disagree, $skipped skipped"

# `[ "$disagree" -eq 0 ]` on its own calls a run that compared nothing a pass.
# Pointing $DASH at a script that answers "syntax error" to everything gave
# "0 agree, 0 disagree, 207 skipped" and exit 0: the authority rejected every
# case in the corpus and the suite reported success. dash is the only thing
# that makes this repo evidence, so a dash that says nothing has to be an
# error, not a quiet pass.
#
# Three things are checked. The corpus is the size it is meant to be; every
# case in it produced a verdict; and the skips stay rare. The corpus count is
# pinned rather than floored for the same reason as wasmi's: a floor only
# catches an empty directory, and this number is the "208 agree" that INDEX.md
# quotes. When a case is added deliberately, this moves in the same commit.
CASES_EXPECTED=243
MAX_SKIPPED=10

cases=$(ls "$here"/cases/*.sh 2>/dev/null | wc -l)
if [ "$cases" -ne "$CASES_EXPECTED" ]; then
	echo "run.sh: corpus holds $cases cases, expected $CASES_EXPECTED" >&2
	echo "  if a case was added or removed on purpose, update CASES_EXPECTED" >&2
	exit 1
fi

ran=$((agree + disagree + skipped - EXPANSION_REFUSALS))
if [ "$ran" -ne "$cases" ]; then
	echo "run.sh: $ran verdicts from $cases cases" >&2
	exit 1
fi

if [ "$skipped" -gt "$MAX_SKIPPED" ]; then
	echo "run.sh: dash rejected $skipped of $cases cases, more than the $MAX_SKIPPED" >&2
	echo "  allowed. The oracle is not answering, so agreement means nothing." >&2
	echo "  Check that \$DASH ($dash) is a working dash." >&2
	exit 1
fi

# The .args sidecar cannot be checked by agreement: if the loop above never
# read it, both shells would see zero arguments and agree with each other all
# day. So the canary case's dash output AS THE LOOP PRODUCED IT is compared
# against a deliberate no-argument run of the same script, and the two are
# required to differ.
#
# An earlier version of this check read the sidecar itself and compared two
# fresh dash runs. It proved the sidecar was not empty and nothing else:
# deleting the sidecar read from the loop left it green.
canary=$here/cases/$CANARY
if [ ! -f "${canary%.sh}.args" ]; then
	echo "run.sh: the canary $CANARY has no .args sidecar" >&2
	exit 1
fi
if [ ! -f "$out/canary.out" ]; then
	echo "run.sh: the canary $CANARY never ran" >&2
	exit 1
fi
rm -rf "$work"; mkdir -p "$work"
( cd "$work" && "$dash" "$canary" ) >"$out/c2.out" 2>/dev/null
if cmp -s "$out/canary.out" "$out/c2.out"; then
	echo "run.sh: the canary produced the same output with and without its" >&2
	echo "  .args sidecar, so the positional-parameter cases are running" >&2
	echo "  with no arguments at all." >&2
	exit 1
fi

[ "$disagree" -eq 0 ]
