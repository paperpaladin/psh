x=1
echo "[$x]"
unset x
echo "[$x]"
echo ${x:-gone}
echo ${x-gone}
