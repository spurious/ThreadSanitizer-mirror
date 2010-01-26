
# OS-specific file name suffixes and command line flags
ifeq ($(OS), linux)
  CXX=g++
  CC=gcc
  LD=g++
  SO=so
  OBJ=o
  EXE=
  O=-o
  LINKO=-o
else ifeq ($(OS), darwin)
  CXX=g++
  CC=gcc
  LD=g++
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
  LDOPT=/DEBUG
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
    OPTFLAGS=/Zi /MT
  else
    OX=O0
    OPTFLAGS=/Od /Zi /MTd
  endif
else
  ifeq ($(OPT), 1)
    OX=O1
    OPTFLAGS=-g -O1
  else
    OX=O0
    OPTFLAGS=-g -O0
  endif
endif

# GTEST_*
GTEST_ROOT=$(SVN_ROOT)/third_party/googletest
ifeq ($(OS), windows)
  ifeq ($(OPT), 1)
    GTEST_BUILD=Release
  else
    GTEST_BUILD=Debug
  endif
  GTEST_LIB=$(GTEST_ROOT)/msvc/gtest/$(GTEST_BUILD)/gtest.lib
else
  GTEST_MAKE_DIR=$(GTEST_ROOT)/make-$(OS)-$(ARCH)
  GTEST_LIB=$(GTEST_MAKE_DIR)/gtest_main.a
endif

ifeq ($(OS), windows)
$(GTEST_LIB):
	cd $(GTEST_ROOT)/msvc && devenv /upgrade gtest.sln && devenv /build $(GTEST_BUILD) /project gtest gtest.sln
else
$(GTEST_LIB):
	mkdir -p $(GTEST_MAKE_DIR) && \
	cd $(GTEST_MAKE_DIR) && \
	make -f ../make/Makefile clean && \
	$(MAKE) -f ../make/Makefile CXXFLAGS="$(ARCHFLAGS)"
endif

.PHONY: GTEST_CLEAN
GTEST_CLEAN:
	rm -rf ${GTEST_ROOT}/msvc/Debug
	rm -rf ${GTEST_ROOT}/msvc/Release
	rm -rf ${GTEST_ROOT}/make-*
