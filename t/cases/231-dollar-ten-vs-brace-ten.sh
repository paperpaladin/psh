# $10 is $1 followed by a literal 0; ${10} is the tenth parameter. Only a
# corpus with more than nine parameters can tell the two apart.
set -- a b c d e f g h i j k l
echo "brace10=[${10}] brace11=[${11}] brace12=[${12}]"
echo "dollar10=[$10] dollar11=[$11] dollar12=[$12]"
echo "brace13=[${13}]"
echo "brace1=[${1}] dollar1=[$1]"
