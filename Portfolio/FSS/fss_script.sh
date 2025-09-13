#!/bin/bash

path=""
command=""

usage() {
    echo "Usage: $0 -p <path> -c <command>"
    echo "Commands:"
    echo "  listAll        : List all directories with timestamps, status, and operation"
    echo "  purge          : Delete the target directory or log file"
}

# Parse arguments
while getopts "p:c:" opt; do
    case ${opt} in
        p)
            path=$OPTARG
            ;;
        c)
            command=$OPTARG
            ;;
        *)
            usage
            exit 1
            ;;
    esac
done

# Check for required args
if [ -z "$path" ] || [ -z "$command" ]; then
    usage
    exit 1
fi

# Ensure path exists if not purging
if [[ "$command" != "purge" && ! -e "$path" ]]; then
    echo "Error: Path '$path' does not exist."
    exit 1
fi

# Command: listAll
list_all() {
    awk -F'[][]' '{
        source=$4;
        target=$6;
        timestamp=$2;
        status=$12;
        if (status == "SUCCESS" || status == "ERROR" || status == "PARTIAL") {
            printf "%s -> %s [Last Sync: %s] [%s]\n", source, target, timestamp, status;
        }
    }' "$path"
}


# Command: purge
purge() {
    # If the path is a log file
    if [ -f "$path" ]; then
        echo "Deleting $path..."
        rm "$path"  # Delete the log file
        echo "Purge complete."
    
    # If the path is a target directory
    elif [ -d "$path" ]; then
        echo "Deleting $path..."
        rm -r "$path"  # Delete the target directory and its contents
        echo "Purge complete."
    # If the path is neither a file nor directory
    else
        echo "Error: '$path' is neither a file nor a directory."
        exit 1
    fi
}

# Main command dispatcher Didn't do the listMonitored and listStopped
case "$command" in
    listAll)
        list_all
        ;;
    purge)
        purge
        ;;
    *)
        echo "Invalid command: $command"
        usage
        exit 1
        ;;
esac
