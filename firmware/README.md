# Build instructions

## Notice

I've only built it on a Debian Linux and on WSL with Ubuntu. I can't give support for other systems, sorry.

## Prerequisites

Any modern Linux distribution, tested on `Debian 12.6` and `Ubuntu 24.04`

Install the required packages:
```console
$ sudo apt update
$ sudo apt install git cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential libyaml-dev libcurl4-openssl-dev
```

Note: `libcurl4-gnutls-dev` can be used instead. It does not matter as no TLS is used in the `memcfg` utility.

## Build

Clone this repository and update the sdk submodule:
```console
$ git clone git@github.com:eduardocasino/kim-1-programmable-memory-card.git
$ cd kim-1-programmable-memory-card
$ git submodule update --init --recursive
```

Go to the `firmware` repository, create the `build` repository and change to it:
```console
$ cd firmware
$ mkdir build
$ cd build
```

Generate the `CMake` files.
```console
PICO_SDK_PATH=../pico-sdk cmake ..
```

Build it with `make`:
```console
$ make
```

If everything went well, you should have a `mememul.uf2` file in the build directory.

Now, go to the tools directory and build the `memcfg`utility:
```console
$ cd ../../tools
$ make
```

Connect the board to your PC with an USB cable. Press the bootloader mode button on the Pico and, while holding it, push the reset button. Release them and your Pico will be in bootloader mode. An `RPI-RP2` removable unit should be now mounted. Transfer the `mememul.uf2` file to its root and wait until the Pico reboots. You should see the green led blinking.

## First configuration

Create the `setup.yaml` and `memmap.yaml` files. You can use the ones in the [README.md](https://github.com/eduardocasino/kim-1-programmable-memory-card/blob/main/tools/README.md#config-file-format) as a template.

Execute the `setup`command:
```console
$ memcfg setup -s setup.yaml -m memmap.yaml -o setup.uf2
```

Put the Pico in bootloader mode again and transfer the `setup.uf2` file. When the Pico reboots, the green led should blink rapidly and turn continuosly on in a few seconds. That means that it has succesfully joined your network and it has been assigned an IP address. Use a terminal program such as `Tera Term` to connect to the pico and write down the assigned address (you will have to press the reset button after you connect through the terminal to force a reboot)

If the led goes on blinking, check the `setup.yaml` file and ensure that your network info is correct.

At this point your card should be ready and you can manage it with the `memcfg` tool. Try this:
```console
$ memcfg config <ip_address>
```

You should see an output similar to this:
```text
---
start: 0x0000
end: 0x13ff
enabled: true
type: ram
---
start: 0x1400
end: 0x177f
enabled: false
type: rom
---
start: 0x1780
end: 0x17ff
enabled: true
type: ram
---
start: 0x1800
end: 0x1fff
enabled: true
type: rom
---
start: 0x2000
end: 0xdfff
enabled: true
type: ram
---
start: 0xe000
end: 0xffff
enabled: true
type: rom
```

You can now disconnect the cable and use the card with your KIM-1!

***NOTE***: It is safe to mantain the card connected to the PC while using it with the KIM. Just do not connect or disconnect the cable while the KIM-1 is turned on, to avoid possible electrostatic discharges. It can be useful if you are trying different default memory maps or modifying the firmware.
