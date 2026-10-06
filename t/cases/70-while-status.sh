i=0
while [ $i -lt 2 ]; do i=$(expr $i + 1); false; done
echo $?
