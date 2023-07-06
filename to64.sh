#!/bin/bash

: > subchaps.h
for i in "$@"; do
	echo "const char *chapter$i = \"$(base64 -w 0 $i)\";" 
	echo
	echo "subchap($i)," >> subchaps.h
done 
