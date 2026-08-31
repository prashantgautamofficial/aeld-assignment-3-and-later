#!/bin/sh

# Validate required arguments
if [ $# -ne 2 ]; then
    echo "Error: expected 2 arguments: <filesdir> <searchstr>" >&2
    exit 1
fi

filesdir=$1
searchstr=$2

# Validate filesdir exists and is a directory
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a directory" >&2
    exit 1
fi

# Count files in directory and subdirectories
file_count=$(find "$filesdir" -type f | wc -l)

# Count matching lines in files with the given search string
match_count=$(grep -R -n -F -- "$searchstr" "$filesdir" 2>/dev/null | wc -l)

# Print required summary message
echo "The number of files are $file_count and the number of matching lines are $match_count"

exit 0
