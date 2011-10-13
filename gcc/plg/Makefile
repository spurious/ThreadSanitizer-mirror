ifeq ($(GCC_VER), )
	GCC_VER=4.6.0
endif

ifeq ($(GCC_DIR), )
	GCC_DIR=../../third_party/gcc-$(VER)/install/lib/gcc/x86_64-unknown-linux-gnu/$(GCC_VER)/plugin/include
endif

OBJS=tsan.o tree-tsan.o
CFLAGS+=-I$(GCC_DIR) -DIN_GCC=1 -DGCC_PLG=1 -fPIC -Wall -std=gnu89
OUT=../lib/libtsan_$(GCC_VER).so

ifeq ($(DEBUG), )
	CFLAGS+=-O3 -DNDEBUG
else
	CFLAGS+=-g3 -D_DEBUG
endif

all: $(OBJS)
	 $(CC) $(OBJS) -shared -o $(OUT)

clean:
	rm *.o
	rm -f $(OUT)

