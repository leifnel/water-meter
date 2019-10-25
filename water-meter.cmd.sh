#!/bin/bash
cd /home/pi/water-meter/
export DISPLAY=:0.0
#/usr/bin/lxterminal -e /home/pi/water-meter/water-meter -di  >>/home/pi/water-meter/water-meter-app.log 2>&1 </dev/null
/home/pi/water-meter/water-meter   >>/home/pi/water-meter/water-meter-app.log 2>&1 </dev/null
/home/pi/water-meter/usbreset /dev/bus/usb/001/004
