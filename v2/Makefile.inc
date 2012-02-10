CXX=clang++
CC=clang
CXXFLAGS=-Wall -fPIE -Werror

ifeq ($(DEBUG), )
	CXXFLAGS+=-O2
else
	CXXFLAGS+=-g -DTSAN_DEBUG=1
endif

