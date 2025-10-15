#!/usr/bin/env bash

# FIX: Enable recursive globbing (**)
shopt -s globstar
shopt -s nullglob

output_file="combined_files.txt"
excluded_array=()

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --exclude)
            if [[ -z "$2" ]]; then
                echo "Error: --exclude option requires a filename." >&2
                exit 1
            fi
            excluded_array+=("$2")
            shift 2
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

> "$output_file" # Clear the output file if it exists

# Reverted to your more robust, general globbing pattern
for file in **/*.c **/*.h **/*.s **/*.peg; do
  # Check if the file should be excluded
  should_exclude=false
  for excluded_file in "${excluded_array[@]}"; do
    # Use a more robust pattern match to handle paths like "src/cpu.c"
    if [[ "$file" == *"$excluded_file" ]]; then
      should_exclude=true
      break
    fi
  done

  if ! $should_exclude && [[ -e "$file" ]]; then
    echo "Processing file: $file" >> "$output_file"
    cat "$file" >> "$output_file"
    echo "" >> "$output_file"
  fi
done
