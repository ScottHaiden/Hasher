CXX := c++
CXXFLAGS := -pthread -std=c++20 -O3 -pipe -march=native -mtune=native
LDFLAGS := -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file_hash.o utils.o common.o

clean:
	rm -f hasher *.o
