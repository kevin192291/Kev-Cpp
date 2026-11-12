---
description: Reads single-letter options (-h, -v, -o FILE, also combined as -vo FILE) with getopts
order: 21
section: arguments
---
# Calls usage() for -h. Afterwards "$@" holds the arguments that aren't options. Inside a function,
# add `local OPTIND` first so a second call starts over.
verbose=false
output=""
while getopts ":hvo:" opt; do
    case "$opt" in
        h)
            usage
            exit 0
            ;;
        v)
            verbose=true
            ;;
        o)
            output="$OPTARG"
            ;;
        :)
            echo "-$OPTARG needs a value" >&2
            exit 1
            ;;
        *)
            echo "Unknown option: -$OPTARG" >&2
            exit 1
            ;;
    esac
done
shift $((OPTIND - 1))
