#!/bin/bash

if [ -z "$1" -o -z "$2" -o -z "$3" ] || [ "$3" != "0" -a "$3" != "1" ]; then
	echo "Sintassi: ./useradd.sh <username> <password> <is_admin>"
	exit 1
fi
if [ $(expr length "$1") -gt 255 -o $(expr length "$2") -gt 255 ]; then
	echo "Username o password troppo lunga (max 255 caratteri)"
	exit 1
fi

echo -e "$1\x1f$2\x1f$3" >> users
