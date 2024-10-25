# sevseg
SEVen SEGment Linux driver (tested on Raspberry Pi 3 - Raspbian <6.1.21-v8+>)

# How-to
```
make dts
make
sudo dtoverlay sevseg.dtbo
sudo insmod sevseg.ko

# set data to display (data is not chars, but raw segments to turn on)
echo -n 'xxx' | sudo tee /sys/class/sevseg/sevseg/data

# set refresh rate to 10ms
echo -n '10' | sudo tee /sys/class/sevseg/sevseg/period
```

# Modifications vs original

Original repo: https://github.com/martinowar/sevseg

This fork, drives an external 74HC164 shift register, that connects with the display.
Also element selection is directly driven by RPI gpios.
