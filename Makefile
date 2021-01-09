CXX := c++
CXXFLAGS += -pthread -std=c++20
LDFLAGS += -lcrypto

.PHONY: all clean

all: hasher

hasher: hasher.cc

clean:
	rm -f hasher
