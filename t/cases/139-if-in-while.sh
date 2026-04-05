i=0
while [ $i -lt 3 ]; do if [ $i = 1 ]; then echo one; else echo $i; fi; i=$(expr $i + 1); done
