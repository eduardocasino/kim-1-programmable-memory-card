# KIM-1 Programmable Memory Board for the MTU Backplane

## **WARNING**

**THIS IS A WORK IN PROGRESS AND MAY CONTAIN ERRORS. USE AT YOUR OWN RISK!!!**

## About

This is a fully programmable RAM/ROM expansion board for MTU backplanes, like the one in the original K-1005 card file, my [expansion card](https://github.com/eduardocasino/kim-1-mtu-expansion-card) or my buffered motherboard.

It uses a Raspberry Pi Pico W to emulate the 64K RAM/ROM memory map, and has some cool features:

* Can emulate RAM and/or ROM at any address of the KIM-1 memory map and with byte granularity so you can individually enable, disable, mark as RW or RO each and every byte of the 64K address space. It can also take over the KIM-1 system RAM and ROM, so it can be used as a diagnostic tool or to relive a KIM-1 with dead RAM.
* A default memory map and data are stored in the Pico flash memory and it is available at boot time.
* Runs a configuration web server with a RESTish API so you can configure and read or write all or any part of memory contents over Wifi and on the fly.
* The board is powered by the KIM-1, but it can olso be dual powered by the KIM-1 and the USB port. This is useful, for example, if you are developing a boot ROM: you can power off the KIM, modify the boot program over wifi and turn it on again to test your changes.
* It emulates an [MTU K-1008 Visable Memory Card](https://github.com/eduardocasino/k-1008-visable-memory-card-replica). It can be mapped to the same addresses as the real one (and change that mapping while running) and gives configurable composite NTSC or PAL output through the RCA connector. VGA output is planned but unimplemented at the moment.
* Emulates an [K-1013 floppy disk controller](http://retro.hansotten.nl/uploads/mtu/MTU%20K-1013%20manual.pdf) with up to four disks, reading and writing to disk images into the SD card (Still in beta)

A python utility, memcfg, is provided to configure the board and facilitate the communication with the REST API. This is an overview of its functionality. Please see the [README.md file](https://github.com/eduardocasino/kim-1-programmable-memory-card/tree/main/tools) for detailed usage instructions:

* Generate configuration files ready to be flashed into the Pico: WiFi parameters, video format and default memory map and contents.
* Get or modify the memory map.
* Read memory contents and generate output in different formats: hexdump, Intel hex, binary, prg and raw. The raw format is how the Pico stores the memory map. Each memory address contains a 16 bit integer. The least significant byte is the content and the most significant one is a bitmap that indicates if the memory is enabled or disabled and writeable or read-only.
* Write memory contents from file or command line. The file formats supported are Intel hex, binary, prg and raw. Also, binary and raw data can be specified as a string from the command line.
* Modify the base address for the K-1008 emulation
* Manages disk images

### [Hardware](https://github.com/eduardocasino/kim-1-programmable-memory-card/tree/main/hardware)
### [Firmware](https://github.com/eduardocasino/kim-1-programmable-memory-card/tree/main/firmware)
### [Tools (memcfg)](https://github.com/eduardocasino/kim-1-programmable-memory-card/tree/main/tools)

***NOTE***: This is a picture of a prototype. The bodges are not needed in the current design.
![prototype](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/images/kim-1-programmable-memory-proto.png?raw=true)

![components](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/images/kim-1-programmable-memory.png?raw=true)
![front](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/images/kim-1-programmable-memory-front.png?raw=true)
![back](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/hardware/images/kim-1-programmable-memory-back.png?raw=true)

## Licensing

This is a personal project that I am sharing in case it is of interest to any retrocomputing enthusiast and all the information in this repository is provided "as is", without warranty of any kind. I am not liable for any damages that may occur, whether it be to individuals, objects, KIM-1 computers, kittens or any other pets. **It should also be noted that everything in this repository is a work in progress, may have not been thoroughly tested and may contain errors, therefore anyone who chooses to use it does so at their own risk**.

[![license](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)

This work is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).

See the LICENSE.md file for details.

## Acknowledgements

* Richard Hulme for his [PicoROM implementation](https://github.com/rhulme/picoROM_pio).
* Jan Cumps on [how to reserve a flash memory block for persistent storage](https://community.element14.com/products/raspberry-pi/b/blog/posts/raspberry-pico-c-sdk-reserve-a-flash-memory-block-for-persistent-storage).
* Jeremy P Bentham for his [PicoWi library](http://iosoft.blog/picowi)
* Alan Reed for his [composite video output implementation](https://github.com/alanpreed/pico-composite-video).
