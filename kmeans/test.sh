#!/bin/bash

echo -e "N_POINTS\tN_THREADS\tTIME\tENERGY" > results.txt


POINTS="2048"
THREADS="1 4 8 64"

for p in $POINTS; do
	for t in $THREADS; do
		echo -ne "$p\t" >> results.txt
		echo -ne "$t\t" >> results.txt
		RES=$(sudo ./kmeans -m 15 -n 15 -p $t -i inputs/random-n$p-d16-c16.txt)
		echo "$RES" | grep "Time" | cut -d ' ' -f 3 | tr -d '\n' >> results.txt
		echo -ne "\t" >> results.txt
		echo "$RES" | grep "Energy" | cut -d ' ' -f 3 >> results.txt
	done
done
