#!/bin/sh
fuser -k 4567/tcp 2>/dev/null 1>/dev/null
sleep 1
./server &
SRV=$!
sleep 1
./client
kill $SRV 2>/dev/null
