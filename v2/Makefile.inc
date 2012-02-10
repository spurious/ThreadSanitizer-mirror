CXX=clang++
CC=clang
CXXFLAGS=-Wall -fPIE -Werror
DEBUG=1

ifeq ($(DEBUG), 0)
	CXXFLAGS+=-O2
else
	CXXFLAGS+=-g -DTSAN_DEBUG=1
endif

