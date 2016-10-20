## uvc-streamer

Simple V4L2 camera capture and streamer

This is modification of original code part from [mjpg-streamer](https://sourceforge.net/projects/mjpg-streamer/)

the addition is YUYV and RGGB formats used in some old cameras

original [README](README)

### Build
````
$ sudo apt-get install libjpeg8-dev
$ make
````

### Cross compile raspberry pi
````
$ wget http://www.ijg.org/files/jpegsrc.v8.tar.gz
$ tar -xzvf jpegsrc.v8.tar.gz
$ cd jpeg-8
$ export PATH=$PATH:$HOME/cross/tools-master/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian/bin
$ ./configure --host=arm-linux CC=arm-linux-gnueabihf-gcc
$ make
$ cd ..
$ make CC=arm-linux-gnueabihf-gcc
````

### Usage
```
check supported format
$ v4l2-ctl -d /dev/videoX --list-formats-ext
start
$ ./uvc_stream -d /dev/videoX -g
all options
$ ./uvc_stream -h
test
vlc http://localhost:8080/
firefox http://localhost:8080/snapshot
```

### License

>uvc-streamer is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; version 2 of the License.

>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
