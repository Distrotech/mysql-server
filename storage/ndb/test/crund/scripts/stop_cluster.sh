#!/bin/bash

./stop_mysqld.sh

# need some extra time
#for ((i=0; i<1; i++)) ; do echo "." ; sleep 1; done

./stop_ndb.sh
