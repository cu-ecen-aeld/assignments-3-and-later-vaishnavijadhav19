#!/bin/bash

# check if the number of arguments passed are 2
if [ $# -ne 2 ]
 then
 echo "invalid number of arguemnets. NUmber of arguments should be 2"
 exit 1
fi


#variables are assigned
filesdir=$1
searchstr=$2

#echo "Directory is: $filesdir"
#echo "Search string is: $searchstr"


#check if  directory exists 
if [ ! -d "$filesdir" ]
then
 echo "the directory do not exist" 
 exit 1
fi


#count number of files in the directory
X=$(find "$filesdir" -type f 2>/dev/null | wc -l)

#Count the number of matching lines
Y=$(grep -R -n -a -- "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $X and the number of matching lines are $Y"




