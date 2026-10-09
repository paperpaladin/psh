# A here-document opened on the last line of a file with no trailing newline.
# The body is whatever follows, which is nothing, and the delimiter never
# arrives. The lexer has to resolve the pending entry at end of input or the
# parser would read the delimiter word itself as the body.
echo before
cat <<EOF