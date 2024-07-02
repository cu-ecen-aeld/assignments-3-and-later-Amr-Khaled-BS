#!/bin/bash

# $#
# $0
# $1-9

# if []
# then
# else
# fi

# greeting=hello or greeting=#2 -> ${greeting}
# files: find . -name a_dir
# content: grep -r "AMR" *

if [ $# -ne 2 ]; then
    echo "enter 2 arguments (filesdir, searchstr)"
    exit 1
fi

if [ -d $1 ]; then
    Y=$(grep -ro $2 $1 | wc -l)
    X=$(find $1 -type f | wc -l)
    echo "The number of files are $X and the number of matching lines are $Y"
else
    echo "enter valid filesdir"
    exit 1
fi
