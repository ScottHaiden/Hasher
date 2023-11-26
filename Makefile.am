bin_PROGRAMS = hasher
hasher_SOURCES = common.cc file.cc hasher.cc platform.cc utils.cc
hasher_CXXFLAGS = -O3 -fpic -pie -march=native -flto
hasher_LDADD = $(LIBCRYPTO_LIBS)

install-exec-hook:
	cd $(DESTDIR)$(bindir) && \
		$(LN_S) hasher checker
