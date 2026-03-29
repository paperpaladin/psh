ls /definitely/not/here 2> err
echo status=$?
cat err > /dev/null
echo ok
