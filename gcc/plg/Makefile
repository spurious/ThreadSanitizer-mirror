ifeq ($(GCC_VER), )
	VER=4.5.3
else
	VER=$(GCC_VER)
endif

ifeq ($(GCC_DIR), )
	GCC_DIR=../../third_party/gcc-$(VER)/install
endif

OBJS=relite.o relite_pass.o relite_ignore.o common_util.o ignore.o ts_util.o
INCLUDE_DIRS=-I../../dynamic_annotations -I../../tsan -I$(GCC_DIR)/lib/gcc/x86_64-unknown-linux-gnu/$(VER)/plugin/include
FLAGS=$(INCLUDE_DIRS) -fPIC -Wall
CFLAGS+=$(FLAGS) -std=c99
CXXFLAGS+=$(FLAGS)

ifeq ($(DEBUG), )
	CFLAGS+=-O3 -DNDEBUG
else
	CFLAGS+=-g3 -D_DEBUG
endif

all: $(OBJS)
	 $(CXX) $(OBJS) -shared -o ../lib/librelite_$(VER).so

%.o: ../../tsan/%.cc
	$(CXX) $(CXXFLAGS) $< -c

clean:
	rm -f *.o
	rm -f ../lib/librelite_$(VER).so
