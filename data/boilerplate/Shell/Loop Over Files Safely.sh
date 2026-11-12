---
description: Goes through files whose names may hold spaces or newlines, with find -print0 or a glob
order: 30
section: files
---
# Every .txt file below this folder. find ends each name with a NUL byte, the one character a file
# name can't contain, so any name arrives whole.
while IFS= read -r -d '' file; do
    echo "$file"
done < <(find . -type f -name '*.txt' -print0)

# Just this folder: nullglob makes a pattern with no matches give nothing rather than itself.
shopt -s nullglob
for file in *.txt; do
    echo "$file"
done
