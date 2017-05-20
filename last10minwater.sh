#!/bin/sh
/usr/local/bin/rrdtool fetch /home/pi/water-meter/water.rrd AVERAGE -a -r 1m -s -10m|awk '!/nan/ {sum+=$2;} END {printf("%7.2f", sum*60)}'
