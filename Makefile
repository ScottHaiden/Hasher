CXX := c++
CXXFLAGS += -pthread -std=c++20
LDFLAGS += -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file_hash.o

clean:
	rm -f hasher *.o
