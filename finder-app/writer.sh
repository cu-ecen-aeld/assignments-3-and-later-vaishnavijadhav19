#!/bin/bash


# check if the number of arguments passed are 2
if [ $# -ne 2 ]
 then
 echo "number of arguements are invalid"
 exit 1
fi

#assign the variables
writefile=$1
writestr=$2

# get the directory path from the file and create it if needed
dir=$(dirname "$writefile")
mkdir -p "$dir"



if ! echo "$writestr" > "$writefile"

 then
   echo "File not created"
   exit 1
fi



