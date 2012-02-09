CXX=g++
CXXFLAGS=-Wall -Werror -fPIE

ifeq ($(DEBUG), )
	CXXFLAGS+=-O2 -DNDEBUG
else
	CXXFLAGS+=-g -D_DEBUG
endif

