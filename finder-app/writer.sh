#!/bin/bash

# $#
# $0
# $1-9

# if []
# then
# else
# fi

if [ $# -ne 2 ]; then
    echo "enter 2 arguments (writefile, writestr)"
    exit 1
fi

mkdir -p $(dirname $1) && touch $1
echo $2 > $1

if [ $? -ne 0 ]; then
    echo "file creation failed"
    exit 1
fi
