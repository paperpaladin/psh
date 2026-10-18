# Positional parameters delivered by the command line, not by `set`. The
# sidecar 209-positional-from-argv.args supplies six arguments, one per line:
# a plain word, a word with a space, a word with a tab, an empty one, a glob
# character, and a last word. t/run.sh gives the same six to both shells.
echo "n=$#"
echo "1=[$1]"
echo "2=[$2]"
echo "3=[$3]"
echo "4=[$4]"
echo "5=[$5]"
echo "6=[$6]"
echo "7=[$7]"
