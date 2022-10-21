# This file is part of Hasher.
#
# Hasher is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# Hasher is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# Hasher. If not, see <https://www.gnu.org/licenses/>.

ARCH := native
CXX := c++
CXXFLAGS := -std=c++20 -O3 -pipe -march=$(ARCH) -mtune=$(ARCH) \
			-pthread -flto -fno-exceptions
LDFLAGS := -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file.o utils.o common.o platform.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ hasher.cc file.o utils.o common.o platform.o

clean:
	rm -f hasher *.o
