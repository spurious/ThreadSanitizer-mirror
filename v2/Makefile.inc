CXX=clang++
CC=clang
CXXFLAGS=-Wall -fPIE -Werror -g
DEBUG=1

ifeq ($(DEBUG), 0)
	CXXFLAGS+=-DTSAN_DEBUG=0 -O2
else
	CXXFLAGS+=-DTSAN_DEBUG=1
endif

