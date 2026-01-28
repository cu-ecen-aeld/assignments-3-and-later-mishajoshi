#!/bin/bash

# Get runtime arguments
filesdir="$1"
searchstr="$2"

# Check if both arguments are provided
if [ -z "$filesdir" ] || [ -z "$searchstr" ]; then
    echo "Error: Both directory path and search string must be provided."
    exit 1
fi

# Check if the first argument is a valid directory
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a directory."
    exit 1
fi

# Count number of files (including subdirectories)
num_files=$(find "$filesdir" -type f | wc -l)

# Count number of matching lines across all files
num_matches=$(grep -r "$searchstr" "$filesdir" | wc -l)

# Print the result
echo "The number of files are $num_files and the number of matching lines are $num_matches"
