CXXFLAGS=-Wall -fPIE -Werror -g $(CFLAGS)
DEBUG=1

ifeq ($(DEBUG), 0)
	CXXFLAGS+=-DTSAN_DEBUG=0 -O3
else
	CXX=clang++
	CC=clang
	CXXFLAGS+=-DTSAN_DEBUG=1
endif

ifneq ($(CXX), g++)
	# clang does not understand flatten
	CXXFLAGS+=-Wno-attributes
endif
