# The exemption belongs to the construct, not to what follows it. A failure in
# a tested position is forgiven; the next command is checked again, including
# when both are in the same function body.
set -e
if false; then :; fi
echo past-if
f() {
	if false; then :; fi
	echo past-inner-if
	false
	echo not-reached
}
f
echo also-not-reached
