include $(top_srcdir)/Makefile.tool.am

noinst_PROGRAMS = 
if VGP_X86_LINUX
noinst_PROGRAMS += testgrind-x86-linux
endif
if VGP_AMD64_LINUX
noinst_PROGRAMS += testgrind-amd64-linux
endif
if VGP_PPC32_LINUX
noinst_PROGRAMS += testgrind-ppc32-linux
endif
if VGP_PPC64_LINUX
noinst_PROGRAMS += testgrind-ppc64-linux
endif
if VGP_PPC32_AIX5
noinst_PROGRAMS += testgrind-ppc32-aix5
endif
if VGP_PPC64_AIX5
noinst_PROGRAMS += testgrind-ppc64-aix5
endif

TESTGRIND_SOURCES_COMMON = tg_main.c

testgrind_x86_linux_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
testgrind_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX)
testgrind_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
testgrind_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
testgrind_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

testgrind_amd64_linux_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
testgrind_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX)
testgrind_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
testgrind_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
testgrind_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

testgrind_ppc32_linux_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
testgrind_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX)
testgrind_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
testgrind_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
testgrind_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

testgrind_ppc64_linux_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
testgrind_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX)
testgrind_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
testgrind_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
testgrind_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

testgrind_ppc32_aix5_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
testgrind_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5)
testgrind_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
testgrind_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
testgrind_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

testgrind_ppc64_aix5_SOURCES      = $(TESTGRIND_SOURCES_COMMON)
testgrind_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
testgrind_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5)
testgrind_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
testgrind_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
testgrind_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)
