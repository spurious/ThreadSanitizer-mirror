CXXFLAGS=-Wall -fPIE -Werror -g
DEBUG=1

ifeq ($(DEBUG), 0)
	CXXFLAGS+=-DTSAN_DEBUG=0 -O3
else
	CXX=clang++
	CC=clang
	# clang does not understand flatten
	CXXFLAGS+=-Wno-attributes
	CXXFLAGS+=-DTSAN_DEBUG=1
endif

