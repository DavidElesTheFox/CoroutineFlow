#!/bin/bash

set -m

OK=0

COMMAND=$1
echo $COMMAND
shift 1
ARGS=$@

while ((OK == 0));
do
	#../../tools/capture-build/tracy-capture -f -o last.tracy&
	#gdb -ex=r --args $@
	echo "# Run: $COMMAND $ARGS"
	$COMMAND "$ARGS"
	OK=$?
	echo "# Waiting for profiling data"
	#jobs
	#fg %1
	if ((OK == 0));
	then
		echo "# Delete last one"
		rm -f last.tracy
	fi
	sleep 1
done
