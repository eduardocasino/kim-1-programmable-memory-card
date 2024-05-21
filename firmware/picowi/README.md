# PicoWi: standalone WiFi driver for the CYW43439 on the Pi Pico W
See https://iosoft.blog/picowi for details

For the Pi Pico RP2040 webcam project, see part 10: http://www.iosoft.blog/picowi_part10

Copyright (c) Jeremy P Bentham 2023

Changes 18/05/2024 by Eduardo Casino https://github.com/eduardocasino :

* Remove all PIO and DMA code
* Add proper support for WiFi country codes
* Add support for POST/PUT/PATCH HTTP methods and large (multi-packet) HTTP requests
