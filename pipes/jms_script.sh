#!/bin/bash

if [ "$#" -ne 4 ] && [ "$#" -ne 5 ]
then
    echo "Usage: ./jms_script.sh -l <path> -c <command>"
    exit 1
fi

pos=0
pathpos=0
commandpos=0
npos=0
for arg
do
    if [ "$arg" = "-l" ]
    then
        pathpos=$((pos+2))
    elif [ "$arg" = "-c" ]
    then
        commandpos=$((pos+2))
        npos=$((pos+3))
    fi
    pos=$((pos+1))
done

if [ "$pathpos" -eq 0 ] 
then
    echo "Error: Missing required flag -l."
    exit 1
fi

if [ "$commandpos" -eq 0 ]
then
    echo "Error: Missing required flag -c."
    exit 1
fi

path=${!pathpos}
command=${!commandpos}

if [ ! -d "$path" ]
then
    echo "Error: Directory '$path' does not exist."
    exit 1
fi

if [ "$command" != "list" ] && [ "$command" != "size" ] && [ "$command" != "purge" ]
then
    echo "Usage: ./jms_script.sh -l <path> -c <command>"
    exit 1
fi

if [ "$command" != "size" ] && [ "$#" -eq 5 ]
then
    echo "Error: The [n] parameter is only allowed with the 'size' command."
    exit 1
elif [ "$command" = "size" ] && [ "$#" -eq 5 ]
then
    n=${!npos}

    if ! [[ "$n" =~ ^[0-9]+$ ]]
    then
        echo "Error: [n] must be a positive integer."
        exit 1
    fi
fi

# Execute command

if [ "$command" = "list" ]
then
    ls -d "$path"/outputs_*/
elif [ "$command" = "size" ]
then
    if [ "$#" -eq 4 ]
    then
        du -sb "$path"/outputs_* | sort -n
    else
        du -sb "$path"/outputs_* | sort -n | tail -n ${n}
    fi
else
    rm -rf "$path"/outputs_*
fi