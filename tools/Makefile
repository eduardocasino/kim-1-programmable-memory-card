# memcfg - A command line utility for managing the Pico KIM-1 Memory Emulator board
#
#   https://github.com/eduardocasino/kim-1-programmable-memory-card
#
#  Copyright (C) 2024 Eduardo Casino
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, Version 3.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA  02110-1301, USA.
#

CC = gcc
TARGET = memcfg
LDFLAGS = -lyaml -lcurl
OBJ = memcfg.o config.o restore.o read.o write.o setup.o http.o resources.o \
		yaml.o uf2.o hexdump.o hexfile.o binfile.o str.o scan.o

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJ)

config.o: globals.h http.h resources.h yaml.h scan.h

restore.o: globals.h http.h resources.h

read.o: globals.h http.h resources.h scan.h hexdump.h hexfile.h binfile.h

write.o: globals.h http.h resources.h scan.h hexfile.h binfile.h str.h

setup.o: globals.h yaml.h uf2.h

http.o: globals.h http.h

resources.o: resources.h

yaml.o: globals.h yaml.h scan.h

uf2.o: globals.h uf2.h

hexdump.o: globals.h

hexfile.o: globals.h scan.h

binfile.o: globals.h

str.o: scan.h globals.h

scan.o: scan.h globals.h

memcfg.o: globals.h config.h restore.h read.h write.h setup.h


