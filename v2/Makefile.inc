CXXFLAGS=-Wall -fPIE -Werror -g $(CFLAGS) -fno-builtin
DEBUG=1

ifeq ($(DEBUG), 0)
	CXXFLAGS+=-DTSAN_DEBUG=0 -O3
else
	CXX=clang++
	CC=clang
	CXXFLAGS+=-DTSAN_DEBUG=1
endif
