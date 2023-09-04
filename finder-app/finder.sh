#!/bin/sh
# finder.sh
# Author: Ben Tait

filesdir=$1
searchstr=$2

# Make sure the two needed args were passed
if [ $# -lt 2 ]
then
	echo "Need a filesdir and searchstr"
	exit 1
fi

# Make sure filesdir is valid directory
if [ ! -d "$filesdir" ]
then
	echo "filesdir not valid!"
	exit 1
fi

# Get number of files and number of matches
numfiles=$(find $filesdir -type f | wc -l)
nummatches=$(grep -r $searchstr $filesdir | wc -l)

# Print out info
echo "The number of files are $numfiles and the number of matching lines are $nummatches"