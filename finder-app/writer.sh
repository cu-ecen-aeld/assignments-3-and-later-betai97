#!/bin/sh
# writer.sh
# Author: Ben Tait

writefile=$1
writestr=$2

# Make sure the two needed args were passed
if [ $# -lt 2 ]
then
	echo "Need a writefile and writestr"
	exit 1
fi

# write the file
mkdir -p $(dirname $writefile)
echo $writestr > $writefile

# exit and print if file couldn't be created
if [ $? != 0 ]
then
	echo "File couldn't be created"
	exit 1
fi