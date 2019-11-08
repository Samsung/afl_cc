#!/bin/sh

# objdump -d "${1}" | grep -Eo '\$0x[0-9a-f]+' | cut -c 2- | sort -u | while read const; do echo $const | python -c 'import sys, struct; sys.stdout.write("".join(struct.pack("<I" if len(l) <= 11 else "<Q", int(l,0)) for l in sys.stdin.readlines()))' > testcases/$const; done
# i=0; strings "${1}"| while read line; do echo -n "$line" > testcases/string_${i} ; i=$[ $i + 1 ] ; done

if [ "$#" -ne 1 ]; then
	echo "Illegal number of parameters"
	echo "$0 /path/to/executable"
	echo "Example: $0 /bin/ls"
	exit 1
fi

EXE="$1"
DICT=$EXE-auto.dict
rm $DICT 2>/dev/null


L=$(objdump -d $EXE | grep -Eo '\$0x[0-9a-f]+' | cut -c 2- | sort -u)

echo "length:" $(echo $L | wc -w) ... Be patient
i=0

# this adds a 0 in front if the value is does not contain an even number of characters
# it also transforms 0x into \x
for v in $L
do
	v=$(echo $v | sed "s/0x//g")
	length=$(echo -n $v | wc -c)
	if [ $((length%2)) -eq 1 ]; then
	    v="0${v}"
	fi

	v="\\x${v}"
	echo auto_value=\"$v\" >> $DICT
	
	i=$((i+1))
	i=$(expr $i % 1000)
	if [ $i -eq 0 ]; then
		echo -n .
	fi
done

