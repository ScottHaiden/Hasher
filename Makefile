ARCH := native
CXX := c++
CXXFLAGS := -O3 -flto -fno-exceptions -march=$(ARCH) -mtune=$(ARCH) -pipe \
			-pthread -std=c++20
LDFLAGS := -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file_hash.o utils.o common.o

clean:
	rm -f hasher *.o
