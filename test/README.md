Programs to test the camera speed, and sd writer speed of the ov2640 and ov5640 cameras on esp32-cam board

https://github.com/espressif/esp32-camera/issues/201#issuecomment-765884077

Replace your normal esp32 additional boards in Arduino - File - Preferences -- with the following to get the 1.05rc6 development version

https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json

These programs create a 15 second mjpeg avi on the SD card for every framesize from 0 to exceeding the framesize for the camera, when the program should fail.

Look at the serial output for the framesizes, speeds, filenames, etc. and on the sd card for the recordings.

