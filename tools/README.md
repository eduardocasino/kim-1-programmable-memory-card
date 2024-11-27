# memcfg - Command line utility for managing the KIM-1 Pico Memory Emulator card

Copyright (C) 2024 Eduardo Casino (https://github.com/eduardocasino) under the terms of the GNU GENERAL PUBLIC LICENSE, Version 3. See the LICENSE.md file for details.

## Build instructions

## Notice

I've only built it on a Debian Linux and on WSL with Ubuntu. I can't give support for other systems, sorry.

## Prerequisites

Any modern Linux distribution, tested on `Debian 12.6` and `Ubuntu 24.04`

Install the required packages (additional to the ones needed for building the firmware):
```console
$ sudo apt update
$ sudo apt install libyaml-dev libcurl4-openssl-dev
```

Note: `libcurl4-gnutls-dev` can be used instead. It does not matter as no TLS is used in the `memcfg` utility.

## Build

Just go to the `tools` directory and build it with `make`:
```console
$ cd tools
$ make
```

## Usage

### General

```text
memcfg [-h] | {read,write,config,restore,setup} ...

    -h                  Shows the general usage help

    read                Read data from the memory emulator
    write               Write data to the memory emulator
    config              Configure address ranges of the memory emulator
    restore             Restore memory map to defaults
    setup               Generates an UF2 file for board configuration
```

### Read command

Dumps data from the Memory Emulation board

```text
memcfg read -h
memcfg read ip_addr -s OFFSET [-c COUNT] [-f {hexdump,bin,ihex,pap,prg,raw}] [-o FILE]

    ip_addr             The IP address of the Pico W in dot-decimal notation, e.g., 192.168.0.10

    -h                  Shows the read command help
    -s/--start OFFSET   Offset in the KIM-1 address space from where read the data.
                        START must be an unsigned integer, either in decimal or hex (0x) notations,
                        in the range 0-65535 (0x0000-0xFFFF)
    -c/--count COUNT    Number of bytes to read. If not present, defaults to 256 (0x100)
                        START must be an unsigned integer, either in decimal or hex (0x) notations,
                        in the range 0-65536 (0x00000-0x10000)
    -f/--format         Format of the dumped data:
                        hexdump     ASCII hex dump in human readable format
                        bin         Binary data
                        ihex        Intel HEX format
                        pap         MOS papertape format
                        prg         Commodore PRG format (binary file with the two first bytes
                                    indicating the data offset)
                        raw         Mainly for debugging purposes. This is the data in the format
                                    it is stored in the board and may change in future revisions.
                                    Each memory location is 16 bit wide. Bits 0 to 7 are the data bits. Bit 8 flags if the location is enabled and Bit 9 flags if the location
                                    is writable.
    -o/--output FILE    FILE to save the data to. Mandatory for binary formats (bin, prg, raw), it
                        defaults to stdout for the rest
```

### Write command

Sends data to the Memory Emulation board

```text
memcfg write -h 
memcfg write ip_addr [-s OFFSET] [-f {bin,ihex,pap,prg,raw}] [-i FILE]|[-d STRING ] [-e]

    ip_addr             The IP address of the Pico W in dot-decimal notation, e.g., 192.168.0.10

    -h                  Shows the write command help
    -s/--start OFFSET   Same as in the read command. Mandatory for bin and raw formats. Ignored
                        for prg and ihex.
    -f/--format         Mandatory. Format of the dumped data:
                        bin         Binary data

                        ihex        Intel HEX format
                        pap         MOS papertape format
                        prg         Commodore PRG format (binary file with the two first bytes
                                    indicating the data offset)
                        raw         Mainly for debugging purposes. This is the data in the format
                                    it is stored in the board and may change in future revisions.
                                    Each memory location is 16 bit wide. Bit 11 flags if the location
                                    is writable, Bit 0 flags if the location is enabled and Bits
                                    11, 10, 6, 5, 4, 3, 2 and 1 are the data bits in reversed order.
    -i/--input FILE     FILE to read the data from. Mandatory for ihex, pap and prg formats.
    -d/--data STRING    String to transfer. Can include hex and oct escaped binary chars. Either a
                        string or an input file must be specified for bin and raw formats. Not valid
                        for ihex, pap or prg.
    -e/--enable         Enables the written address block
```

### Config command

Configures the emulated memory map

```text
memcfg config -h

    -h                      Shows the config command help

memcfg config ip_addr [-o FILE]

    ip_addr             The IP address of the Pico W in dot-decimal notation, e.g., 192.168.0.10

    Without extra arguments, prints the memory map configuration to stdout

    -o/--output FILE        File to save the config to. If not specified, defaults to stdout

memcfg config ip_addr [-d RANGE [-d RANGE ...]] [-e RANGE [-e RANGE ...]]
                             [-r RANGE [-r RANGE ...]] [-w RANGE [-w RANGE ...]] [-v OFFSET] [-i FILE] [-o FILE]

    RANGE                   The address range(s) to apply each option. The format is
                            0xHHHH-0xHHHH, where HHHH are hexadecimal numbers
    -d/--disable RANGE      Sets the specified RANGE as disabled. The data bus will remain
                            in high impedance state when it is addressed.
    -e/--enable RANGE       Sets the specified RANGE as enabled.
    -r/--readonly RANGE     Configures the RANGE as ROM
    -w/--writable RANGE     Configures the RANGE as RAM
    -v/--video OFFSET       Video memory start address
    -i/--input FILE         Uses yaml FILE for configuration. See the config file format below.
    -o/--output FILE        File to save the config to 

Options with RANGEs may be specified multiple times. The options are interpreted in the following order:
FILE, disable, enable, readonly and writable. So, for example:

    memcfg ip_addr config -r 0xe000-0xffff -e 0x0400-0x13ff -w 0x0400-0x13ff -e 0xe000-0xffff -d 0x0000-0xfffff

will first mark the whole memory map as disabled, will then enable the ranges 0x400-0x13ff and
0xe000-0xffff, then mark 0xe000-0xffff as ROM and, finally, set 0x400-0x13ff as RAM.
```

### Restore command

Restore memory map to defaults.

```text
memcfg restore -h | ip_addr

    ip_addr                 The IP address of the Pico W in dot-decimal notation, e.g., 192.168.0.10
    -h                      Shows the config command help
```

### Setup command

Generates UF2 configuration file.

```text
memcfg setup -h
memcfg setup [-s FILE] [-m FILE] -o FILE

    -h                      Shows the config command help
    -s/--setup FILE         Setup configuration file. See format below
    -m/--memory FILE        Default memory map file. Same format as the config filr
    -o/--output FILE        Generated UF2 file

At least one of '-m' or '-s' files must be specified.
```

### Config file format

The config file is just a YAML document with three keys, all mandatory:

```yaml
---
wifi:
 country: <countrycode>              # Standard 2 or 3 character country code, like 'ES' or 'FR' 
 ssid: mywifisid                     # Your wifi SID
 password: mysupersecretwifipassword # Your wifi password
video:                               # K-1008 Video Card emulation
 system: <video_system>              # 'ntsc' or 'pal'
 address: <integer>                  # Offset address of the video memory
fdc:                                 # K-1013 FDC emulation
 enabled: <bool>                     # Wether if the FDC emulation is enabled or not
 usrram: <integer>                   # Address of the User DMA RAM block
 sysram: <integer>                   # Address of the System DMA RAM block
 optswitch: <bool>                   # Optional: Enable/disable the card "options" switch.
 disk0:                              # Optional: Image to mount at boot as disk0
  file: "filename"                   #   Image file name
  ro: <bool>                         #   Optional: Read.only Flag. False by default
 disk1:                              # Optional: Same for disks 1 to 3
  ...
 disk2:
  ...
 disk3:
```

The valid adresses for the K-1008 and K-1013 emulation are: `0x2000, 0x4000, 0x6000, 0x8000, 0xA000 and 0xC000`

### Memory config file format

**memcfg** uses a simple YAML file for configuration. Each section is a YAML document and they are processed in the same order as they appear in the document. Three hyphens mark the beginning of a new document. Text after a '#' are comments and not processed. Valid *key:value* pairs are:

```yaml
start: <integer>        # Mandatory. Offset address for this section. 
end: <integer>          # Mandatory if no'file' nor 'data' are present. End
                        #           address of the section.
enabled: true|false     # Optional. Marks the section as enabled or disabled.
type: ram | rom         # Optional. Marks the section as RAM or ROM.
file: 'filename'        # Optional. Initializes section with the file contents.
                        #           Must be binary.
                        #           if the file is bigger than the sections, it
                        #           is truncated and a warning is issued.
fill: <byte>            # Optional. Fills the section with <byte>. If file is 
                        #           also specified and its smaller than the
                        #           section, fills the remaining space.
data: 'string'          # Optional. Fills the section with the string contents. Hex
                        #           and Oct escape codes (like '\xFF' or '\177') are
                        #           supported
```

Example:

```yaml
---
# Disable the whole address map by default
start: 0x0000
end: 0xffff
enabled: false
type: rom
fill: 0x00
---
# System RAM + ram hole
start: 0x0000
end: 0x13ff
enabled: true
type: ram
---
# RRIOT RAM
start: 0x1780
end: 0x17f9
enabled: true
type: ram
---
# Interrupt vectors
start: 0x17fa
end: 0x17ff
enabled: true
type: ram
data: "\x00\x1c\x00\x00\x00\x1c"
---
# System ROM
start: 0x1800
end: 0x1fff
enabled: true
type: rom
file: "kim.bin"
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
file: "customrom.bin"
fill: 0xff
---
# Put KIM vectors at the end
# NOTE: The memory card does not manage the K7 line,
#       so the interrupt vectors must ALWAYS be present
#       at 0xFFFA-0xFFFF
type: rom
start: 0xFFFA
data: "\x1c\x1c\x22\x1c\x1f\x1c"
enabled: true
```
