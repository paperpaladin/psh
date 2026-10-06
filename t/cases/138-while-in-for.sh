for i in 1 2; do j=0; while [ $j -lt 2 ]; do echo $i.$j; j=$(expr $j + 1); done; done
