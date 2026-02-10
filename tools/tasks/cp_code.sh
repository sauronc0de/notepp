#!/bin/bash

# Usage: ./script.sh /path/to/folder output_file.txt

input_folder="$1"
output_file="$2"

if [ -z "$input_folder" ] || [ -z "$output_file" ]; then
    echo "Usage: $0 /path/to/folder output_file.txt"
    exit 1
fi

# Clear or create the output file
> "$output_file"

# Loop through all files in the folder
for file in "$input_folder"/*; do
    if [ -f "$file" ]; then
        echo "========== $(basename "$file") ==========" >> "$output_file"
        cat "$file" >> "$output_file"
        echo -e "\n\n" >> "$output_file"
    fi
done

echo "All files from $input_folder have been written to $output_file"
code --goto "$output_file":1