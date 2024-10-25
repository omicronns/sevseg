#!/bin/bash

make
make dts
sudo rmmod sevseg
sudo dtoverlay sevseg.dtbo
sudo insmod sevseg.ko
echo -en '\xc0\xdd\xa4' | sudo tee /sys/class/sevseg/sevseg/data
