PLATFORM=$(shell uname -s | sed -e "s/CYGWIN.*/Windows/" | sed -e "s/Darwin/Mac/")
BITNESS=$(shell uname -m | sed -e "s/i.86/32/;s/x86_64/64/;s/amd64/64/;s/arm.*/arm/")
MAJOR_V=$(shell uname -r | sed -e "s/\([0-9]*\)\..*/\1/")

ifeq ($(PLATFORM)$(BITNESS), Linux64)
default: l64 l32
endif

ifeq ($(PLATFORM)$(BITNESS), Linux32)
default: l32
endif


ifeq ($(PLATFORM)$(BITNESS), Windows32)
default: w
endif

ifeq ($(PLATFORM), Mac)
ifeq ($(MAJOR_V), 9)
default: m32
endif
ifeq ($(MAJOR_V), 10)
default: m64 m32
endif
DARWIN_VERSION=darwin_$(MAJOR_V)
endif

dummy:
	@echo "Can't determine default configuration!"
	@echo "For 64-bit Linux build: make l64"
	@echo "For 32-bit Linux build: make l32"
	@echo "For mixed  Linux build: make l"
	@echo "For 64-bit Mac   build: make m64"
	@echo "For 32-bit Mac   build: make m32"
	@echo "For mixed  Mac   build: make m"
	@echo "For ARM    Linux build: make la"
	@exit 1

# This should go after the platform/bitness detection code, otherwise
# we'll have a wrong default target from gtest...
.PHONY: all
all:

# OS-specific file name suffixes and command line flags
ifeq ($(OS), linux)
  CXX=g++
  CC=gcc
  LD=g++
  LTLD=g++
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-o
else ifeq ($(OS), darwin)
  CXX=g++
  CC=gcc
  LD=g++
ifeq ($(ARCH), x86)
  LTLD=$(VALGRIND_ROOT)/coregrind/link_tool_exe_darwin 0x38000000 g++
endif
ifeq ($(ARCH), amd64)
  LTLD=$(VALGRIND_ROOT)/coregrind/link_tool_exe_darwin 0x138000000 g++
endif
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-Wl,-o,
else ifeq ($(OS), windows)
  CXX=cl
  CC=cl
  LD=link
  SO=dll
  OBJ=obj
  EXE=.exe
  O=/Fo
  LINKO=/OUT:
  LDOPT=/DEBUG /INCREMENTAL:NO
else
  OS=UNKNOWN_OS
endif

# STATICFLAGS
ifeq ($(STATIC), 1)
  S=-static
  STATICFLAGS=-static
else
  S=
  STATICFLAGS=
endif

# ARCHFLAGS
ifeq ($(OS), windows)
  ARCHFLAGS=
else
  ifeq ($(ARCH), x86)
    ARCHFLAGS=-m32
  else ifeq ($(ARCH), amd64)
    ARCHFLAGS=-m64
  else
    ARCH=UNKNOWN_ARCH
  endif
endif

# OPTFLAGS
ifeq ($(OS), windows)
  ifeq ($(OPT), 1)
    OX=O1
    OPTFLAGS=/Zi /MT /O1 /Ob0
  else
    OX=O0
    OPTFLAGS=/Od /Zi /MTd
  endif
else
  ifeq ($(OPT), 1)
    OX=O1
    OPTFLAGS=-g -O1 -fno-inline -fno-omit-frame-pointer -fno-builtin
  else
    OX=O0
    OPTFLAGS=-g -O0
  endif
endif

# GTEST_*
GTEST_ROOT=$(SVN_ROOT)/third_party/googletest
GTEST_CXXFLAGS=$(CXXFLAGS) $(ARCHFLAGS)
ifeq ($(OS), windows)
  ifeq ($(OPT), 1)
    GTEST_BUILD=Release
    GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/$(GTEST_BUILD)/gtest.lib
  else
    GTEST_BUILD=Debug
    GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/$(GTEST_BUILD)/gtestd.lib
  endif
else
  GTEST_MAKE_DIR=$(GTEST_ROOT)/make-$(OS)-$(ARCH)$(EXTRA_BUILD_SUFFIX)
  GTEST_LIB=$(GTEST_MAKE_DIR)/gtest_main.a
endif

ifeq ($(OS), windows)
$(GTEST_LIB):
	cd $(GTEST_ROOT)/msvc && devenv /upgrade gtest.sln && devenv /build $(GTEST_BUILD) /project gtest gtest.sln
else
$(GTEST_LIB):
	mkdir -p $(GTEST_MAKE_DIR) && \
	cd $(GTEST_MAKE_DIR) && \
	$(MAKE) -f ../make/Makefile gtest_main.a CXXFLAGS="$(GTEST_CXXFLAGS)" LDFLAGS=$(GTEST_LDFLAGS)
endif

.PHONY: GTEST_CLEAN
GTEST_CLEAN:
	rm -rf ${GTEST_ROOT}/msvc/gtest/Debug
	rm -rf ${GTEST_ROOT}/msvc/gtest/Release
	rm -rf ${GTEST_ROOT}/make-*
