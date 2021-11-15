ARCH := native
CXX := c++
CXXFLAGS := -std=c++20 -O3 -pipe -march=$(ARCH) -mtune=$(ARCH) \
			-pthread -flto -fno-exceptions

LDFLAGS := -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file_hash.o utils.o common.o

clean:
	rm -f hasher *.o
