bin_PROGRAMS = hasher
hasher_SOURCES = common.cc file.cc hasher.cc platform.cc utils.cc
hasher_CXXFLAGS = -O3 -fpic -pie -march=native -flto -std=c++20
hasher_LDADD = $(LIBCRYPTO_LIBS)

install-exec-hook:
	cd $(DESTDIR)$(bindir) && (test -L checker || $(LN_S) hasher checker)
