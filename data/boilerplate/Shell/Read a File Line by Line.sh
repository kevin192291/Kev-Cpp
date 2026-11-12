---
description: Reads a file one line at a time, keeping spaces and backslashes and the last line without a newline
order: 31
section: files
---
# IFS= keeps leading and trailing spaces, -r keeps backslashes, and the || part still reads a last line
# that has no newline after it. (mapfile -t lines < input.txt reads them all into an array instead.)
while IFS= read -r line || [[ -n "$line" ]]; do
    echo "$line"
done < input.txt
