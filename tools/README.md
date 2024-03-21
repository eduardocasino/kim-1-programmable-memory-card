# memcfg - Command line utility for managing the KIM-1 Pico Memory Emulator card

Copyright (C) 2024 Eduardo Casino (https://github.com/eduardocasino) under the terms of the GNU GENERAL PUBLIC LICENSE, Version 2. See the LICENSE.md file for details.

## Usage

### General

```
memcfg [-h] ip_addr {read,write,config} ...

    -h                  Shows the general usage help
    ip_addr             The IP address of the Pico W in dot-decimal notation, e.g., 192.168.0.10

    read                Read data commmand
    write               Send data command
    config              Configuration command
```

### Read command

Dumps data from the Memory Emulation board

```
memcfg ip_addr read [-h] -s OFFSET [-c COUNT] [-f {hexdump,bin,ihex,prg,raw}] [-o FILE]

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
                        prg         Commodore PRG format (binary file with the two first bytes
                                    indicating the data offset)
                        raw         Mainly for debugging purposes. This is the data in the format
                                    it is stored in the board and may change in future revisions.
                                    Each memory location is 16 bit wide. Bit 11 flags if the location
                                    is writable, Bit 0 flags if the location is enabled and Bits
                                    11, 10, 6, 5, 4, 3, 2 and 1 are the data bits in reversed order.
    -o/--output FILE    FILE to save the data to. Mandatory for binary formats (bin, prg, raw), it
                        defaults to stdout for the rest
```

### Write command

Sends data to the Memory Emulation board

```
memcfg ip_addr write [-h] [-s OFFSET] [-f {bin,ihex,prg,raw}] [-i FILE] [-e] [string]

    -h                  Shows the write command help
    -s/--start OFFSET   Same as in the read command
    -f/--format         Format of the dumped data:
                        bin         Binary data

                        ihex        Intel HEX forma
                        prg         Commodore PRG format (binary file with the two first bytes
                                    indicating the data offset)
                        raw         Mainly for debugging purposes. This is the data in the format
                                    it is stored in the board and may change in future revisions.
                                    Each memory location is 16 bit wide. Bit 11 flags if the location
                                    is writable, Bit 0 flags if the location is enabled and Bits
                                    11, 10, 6, 5, 4, 3, 2 and 1 are the data bits in reversed order.
    -i/--input FILE     FILE to read the data from. Mandatory for ihex and prg formats.
    -e/--enable         Enables the written address block
    string              String to transfer. Can include escaped binary chars. Either a string or an
                        input file must be specified for ihex and prg formats. Not valid for
                        ihex or prg.
```

### Config command

Configures the emulated memory map

```
memcfg ip_addr config [-o FILE]

    Without extra arguments, prints the memory map configuration to stdout

    -o/--output FILE        File to save the config to. If not specified, defaults to stdout

memcfg ip_addr config [-h] [-d RANGE [RANGE ...]] [-e RANGE [RANGE ...]]
                             [-r RANGE [RANGE ...]] [-w WRITABLE [RANGE ...]] [-i FILE]

    RANGE                   The address range(s) to apply each option. The format is
                            0xHHHH-0xHHHH, where HHHH are hexadecimal numbers
    -h                      Shows the config command help
    -d/--disable RANGE      Sets the specified RANGE as disabled. The data bus will remain
                            in high impedance state when it is addressed.
    -e/--enable RANGE       Sets the specified RANGE as enabled.
    -r/--readonly RANGE     Configures the RANGE as ROM
    -w/--writable RANGE     Configures the RANGE as RAM
    -i/--input FILE         Uses yaml FILE for configuration. See the config file format below, 

RANGEs may be specified multiple times. The options are interpreted in the following order:
FILE, disable, enable, readonly and writable. So, for example:

    memcfg ip_addr config -r 0xe000-0xffff -e 0x0400-0x13ff 0xe000-0xffff -w 0x0400-0x13ff -d 0x0000-0xfffff

will first mark the whole memory map as disabled, will then enable the ranges 0x400-0x13ff and
0xe000-0xffff, then mark 0xe000-0xffff as ROM and, finally, set 0x400-0x13ff as RAM.
```

### Config file format

**memcfg** uses a simple YAML file for configuration. Each section is a YAML document and they are processed in the same order as they appear in the document. Three hyphens mark the beginning of a new document. Text after a '#' are comments and not processed. Valid *key:value* pairs are:

```yaml
start: <integer>        # Mandatory. Offset address for this section. 
end: <integer>          # Mandatory. End address of the section.
enabled: true|false     # Optional. Marks the section as enabled or disabled.
type: ram | rom         # Optional. Marks the section as RAM or ROM.
file: 'filename'        # Optional. Initializes section with the file contents.
                        #           Must be binary.
                        #           if the file is bigger than the sections, it
                        #           is truncated and a warning is issued.
fill: <byte>            # Optional. Fills the section with <byte>. If file is 
                        #           also specified and its smaller than the
                        #           section, fills the remaining space.
```

Example:

```yaml
---
start: 0x0000
end: 0x17ff
enabled: false
type: ram
---
start: 0x1800
end: 0x1fff
enabled: true
type: rom
file: "kim.bin"
---
start: 0x3000
end: 0xdfff
enabled: true
type: ram
---
start: 0xe000
end: 0xffff
enabled: true
type: rom
fill: 0x55
```
