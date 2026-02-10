echo "🧩 Show dependences 🧩"

remove_alone_libraries() {
  # Specify the file path
  file_path=$1

  # Temporary file to store the modified content
  temp_file=$(mktemp)

  # Loop through each line of the file
  while IFS= read -r line; do
    # Check if the line contains the word "node"
    if [[ $line == *"node"* ]]; then
      # Extract the name inside quotes (if any)
      if [[ $line =~ \"([^\"]+)\" ]]; then
        name="${BASH_REMATCH[1]}"

        # Count occurrences of the name in the file
        count=$(grep -o "\"$name\"" "$file_path" | wc -l)

        # If the name appears more than once, write the line to the temporary file
        if [ "$count" -gt 1 ]; then
          echo "$line" >> "$temp_file"
        fi
      else
        # If the line doesn't contain a quoted name, write it to the temp file
        echo "$line" >> "$temp_file"
      fi
    else
      # If the line doesn't contain the word "node", write it to the temp file
      echo "$line" >> "$temp_file"
    fi
  done < "$file_path"

  # Replace the original file with the modified content
  mv "$temp_file" "$file_path"

  echo "Processing completed. Lines with unique names inside quotes have been removed."
}


# Start the script
echo "🍝 Generate graph dependences 🍝"

# Create the build directory if it doesn't exist
echo "📁 Creating build folder ..." 
mkdir -p  build 
cd build

# Step 1: Generate the DOT file using CMake
echo "Generating dependency graph DOT file..."
cmake .. --graphviz=dependencies.dot -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_UNIT_TEST=OFF -DENABLE_EDF_TEST=OFF

# Create the graphviz directory if it doesn't exist
mkdir -p graphviz

# Step 2: Remove some parts to generate a cleaner graph
echo "Cleaning Legend, Mock, Test, and Example from the DOT file..."
cp dependencies.dot dependencies_filtered.dot
sed -i '/subgraph clusterLegend {/,/}/d' dependencies_filtered.dot
sed -i '/mock/d' dependencies_filtered.dot
sed -i '/test/d' dependencies_filtered.dot
sed -i '/example/d' dependencies_filtered.dot
sed -i '/ef_ed/d' dependencies_filtered.dot

remove_alone_libraries dependencies_filtered.dot

# Step 3: Convert the DOT file to PNG
echo "Converting DOT file to PNG image..."
dot -Tpng dependencies.dot -o dependencies.png
dot -Tpng dependencies_filtered.dot -o dependencies_filtered.png

# Move the DOT file to the graphviz directory
mv dependencies.dot* graphviz/
mv dependencies_filtered.dot* graphviz/ && code dependencies_filtered.png

