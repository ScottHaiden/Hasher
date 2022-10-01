ARCH := native
CXX := c++
CXXFLAGS := -std=c++20 -O3 -pipe -march=$(ARCH) -mtune=$(ARCH) \
			-pthread -flto -fno-exceptions
LDFLAGS := -lcrypto -fpic -pie

.PHONY: all clean

all: hasher

hasher: hasher.cc file.o utils.o common.o xattr.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ hasher.cc file.o utils.o common.o xattr.o

clean:
	rm -f hasher *.o
