---
description: The folder the script itself is in, to find files next to it wherever it's run from
order: 32
section: files
---
# Works when the script is run from another folder or through a relative path.
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
