#!/bin/bash

# Get runtime arguments
writefile="$1"
writestr="$2"

# Check if both arguments are provided
if [ -z "$writefile" ] || [ -z "$writestr" ]; then
    echo "Error: Both file path and string must be provided."
    exit 1
fi

# Extract the directory path from the file path
dirpath=$(dirname "$writefile")

# Create the directory path if it does not exist
if ! mkdir -p "$dirpath"; then
    echo "Error: Failed to create directory path '$dirpath'."
    exit 1
fi

# Write the string to the file, overwriting if it exists
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Failed to write to file '$writefile'."
    exit 1
fi

# Success message (optional)
echo "Successfully wrote to '$writefile'."
