#!/bin/bash
while [ 1=1 ]
do
/home/pi/water-meter/water-meter >>/home/pi/water/water-meter-app.log 2>&1 </dev/null
/home/pi/water-meter/usbreset /dev/bus/usb/001/004
done
