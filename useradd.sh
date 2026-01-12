#!/bin/bash

if [ -z "$1" -o -z "$2" -o -z "$3" ]; then
	echo "Sintassi: ./useradd.sh <username> <password> <is_admin>"
	exit 1
fi

echo -e "$1\x1f$2\x1f$3" >> users
