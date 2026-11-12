---
description: Reads short and long options (-v, --output FILE, --output=FILE) and leaves the other arguments in "$@"
order: 20
section: arguments
---
# Calls usage() for --help. Afterwards "$@" holds the arguments that aren't options.
verbose=false
output=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -h | --help)
            usage
            exit 0
            ;;
        -v | --verbose)
            verbose=true
            ;;
        -o | --output)
            if [[ $# -lt 2 ]]; then
                echo "$1 needs a value" >&2
                exit 1
            fi
            output="$2"
            shift
            ;;
        --output=*)
            output="${1#*=}"
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            break
            ;;
    esac
    shift
done
