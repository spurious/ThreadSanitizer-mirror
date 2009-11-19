include $(top_srcdir)/Makefile.tool.am

noinst_PROGRAMS = 
noinst_DSYMS =

if VGCONF_PLATFORMS_INCLUDE_X86_LINUX
noinst_PROGRAMS += tsan-x86-linux vgpreload_tsan-x86-linux.so
endif
if VGCONF_PLATFORMS_INCLUDE_AMD64_LINUX
noinst_PROGRAMS += tsan-amd64-linux vgpreload_tsan-amd64-linux.so
endif
if VGCONF_PLATFORMS_INCLUDE_PPC32_LINUX
noinst_PROGRAMS += tsan-ppc32-linux vgpreload_tsan-ppc32-linux.so
endif
if VGCONF_PLATFORMS_INCLUDE_PPC64_LINUX
noinst_PROGRAMS += tsan-ppc64-linux vgpreload_tsan-ppc64-linux.so
endif
if VGCONF_PLATFORMS_INCLUDE_PPC32_AIX5
noinst_PROGRAMS += tsan-ppc32-aix5 vgpreload_tsan-ppc32-aix5.so
endif
if VGCONF_PLATFORMS_INCLUDE_PPC64_AIX5
noinst_PROGRAMS += tsan-ppc64-aix5 vgpreload_tsan-ppc64-aix5.so
endif

if VGCONF_PLATFORMS_INCLUDE_X86_DARWIN
noinst_PROGRAMS += tsan-x86-darwin vgpreload_tsan-x86-darwin.so
noinst_DSYMS    +=                     vgpreload_tsan-x86-darwin.so
endif

VGPRELOAD_TSAN_SOURCES_COMMON = ts_valgrind_intercepts.c 

vgpreload_tsan_x86_linux_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_x86_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
vgpreload_tsan_x86_linux_so_CFLAGS       = $(AM_CFLAGS_X86_LINUX) $(AM_CFLAGS_PIC)
vgpreload_tsan_x86_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_LINUX)
vgpreload_tsan_x86_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_X86_LINUX) 

#$(LIBREPLACEMALLOC_LDFLAGS_X86_LINUX)

vgpreload_tsan_amd64_linux_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_amd64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
vgpreload_tsan_amd64_linux_so_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_tsan_amd64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_AMD64_LINUX)
vgpreload_tsan_amd64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_AMD64_LINUX) 

#$(LIBREPLACEMALLOC_LDFLAGS_AMD64_LINUX)

vgpreload_tsan_ppc32_linux_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_ppc32_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
vgpreload_tsan_ppc32_linux_so_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX) $(AM_CFLAGS_PIC)
vgpreload_tsan_ppc32_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_LINUX)
vgpreload_tsan_ppc32_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_LINUX) 

#$(LIBREPLACEMALLOC_LDFLAGS_PPC32_LINUX)

vgpreload_tsan_ppc64_linux_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_ppc64_linux_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
vgpreload_tsan_ppc64_linux_so_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX) $(AM_CFLAGS_PIC)
vgpreload_tsan_ppc64_linux_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_LINUX)
vgpreload_tsan_ppc64_linux_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_LINUX) 

#	$(LIBREPLACEMALLOC_LDFLAGS_PPC64_LINUX)

vgpreload_tsan_ppc32_aix5_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_ppc32_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
vgpreload_tsan_ppc32_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5) $(AM_CFLAGS_PIC)
vgpreload_tsan_ppc32_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC32_AIX5)
vgpreload_tsan_ppc32_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC32_AIX5) 

#$(LIBREPLACEMALLOC_LDFLAGS_PPC32_AIX5)

vgpreload_tsan_ppc64_aix5_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_ppc64_aix5_so_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
vgpreload_tsan_ppc64_aix5_so_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5) $(AM_CFLAGS_PIC)
vgpreload_tsan_ppc64_aix5_so_DEPENDENCIES = $(LIBREPLACEMALLOC_PPC64_AIX5)
vgpreload_tsan_ppc64_aix5_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_PPC64_AIX5) 

#$(LIBREPLACEMALLOC_LDFLAGS_PPC64_AIX5)

vgpreload_tsan_x86_darwin_so_SOURCES      = $(VGPRELOAD_TSAN_SOURCES_COMMON)
vgpreload_tsan_x86_darwin_so_CPPFLAGS     = $(AM_CPPFLAGS_X86_DARWIN)
vgpreload_tsan_x86_darwin_so_CFLAGS       = $(AM_CFLAGS_X86_DARWIN) $(AM_CFLAGS_PIC)
vgpreload_tsan_x86_darwin_so_DEPENDENCIES = $(LIBREPLACEMALLOC_X86_DARWIN)
vgpreload_tsan_x86_darwin_so_LDFLAGS      = \
					      $(PRELOAD_LDFLAGS_X86_DARWIN)


TSAN_SOURCES_COMMON = ts_valgrind.cc thread_sanitizer.cc ts_util.cc

tsan_x86_linux_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_x86_linux_CPPFLAGS     = $(AM_CPPFLAGS_X86_LINUX)
tsan_x86_linux_CFLAGS       = $(AM_CFLAGS_X86_LINUX)
tsan_x86_linux_CXXFLAGS       = $(AM_CXXFLAGS_X86_LINUX)
tsan_x86_linux_DEPENDENCIES = $(COREGRIND_LIBS_X86_LINUX)
tsan_x86_linux_LDADD        = $(TOOL_LDADD_X86_LINUX)
tsan_x86_linux_LDFLAGS      = $(TOOL_LDFLAGS_X86_LINUX)

tsan_amd64_linux_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_amd64_linux_CPPFLAGS     = $(AM_CPPFLAGS_AMD64_LINUX)
tsan_amd64_linux_CFLAGS       = $(AM_CFLAGS_AMD64_LINUX)
tsan_amd64_linux_CXXFLAGS       = $(AM_CXXFLAGS_AMD64_LINUX)
tsan_amd64_linux_DEPENDENCIES = $(COREGRIND_LIBS_AMD64_LINUX)
tsan_amd64_linux_LDADD        = $(TOOL_LDADD_AMD64_LINUX)
tsan_amd64_linux_LDFLAGS      = $(TOOL_LDFLAGS_AMD64_LINUX)

tsan_ppc32_linux_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_ppc32_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_LINUX)
tsan_ppc32_linux_CFLAGS       = $(AM_CFLAGS_PPC32_LINUX)
tsan_ppc32_linux_CXXFLAGS       = $(AM_CXXFLAGS_PPC32_LINUX)
tsan_ppc32_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_LINUX)
tsan_ppc32_linux_LDADD        = $(TOOL_LDADD_PPC32_LINUX)
tsan_ppc32_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_LINUX)

tsan_ppc64_linux_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_ppc64_linux_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_LINUX)
tsan_ppc64_linux_CFLAGS       = $(AM_CFLAGS_PPC64_LINUX)
tsan_ppc64_linux_CXXFLAGS       = $(AM_CXXFLAGS_PPC64_LINUX)
tsan_ppc64_linux_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_LINUX)
tsan_ppc64_linux_LDADD        = $(TOOL_LDADD_PPC64_LINUX)
tsan_ppc64_linux_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_LINUX)

tsan_ppc32_aix5_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_ppc32_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC32_AIX5)
tsan_ppc32_aix5_CFLAGS       = $(AM_CFLAGS_PPC32_AIX5)
tsan_ppc32_aix5_CXXFLAGS       = $(AM_CXXFLAGS_PPC32_AIX5)
tsan_ppc32_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC32_AIX5)
tsan_ppc32_aix5_LDADD        = $(TOOL_LDADD_PPC32_AIX5)
tsan_ppc32_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC32_AIX5)

tsan_ppc64_aix5_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_ppc64_aix5_CPPFLAGS     = $(AM_CPPFLAGS_PPC64_AIX5)
tsan_ppc64_aix5_CFLAGS       = $(AM_CFLAGS_PPC64_AIX5)
tsan_ppc64_aix5_CXXFLAGS       = $(AM_CXXFLAGS_PPC64_AIX5)
tsan_ppc64_aix5_DEPENDENCIES = $(COREGRIND_LIBS_PPC64_AIX5)
tsan_ppc64_aix5_LDADD        = $(TOOL_LDADD_PPC64_AIX5)
tsan_ppc64_aix5_LDFLAGS      = $(TOOL_LDFLAGS_PPC64_AIX5)

tsan_x86_darwin_SOURCES      = $(TSAN_SOURCES_COMMON)
tsan_x86_darwin_CPPFLAGS     = $(AM_CPPFLAGS_X86_DARWIN)
tsan_x86_darwin_CFLAGS       = $(AM_CFLAGS_X86_DARWIN) -O2 -fno-weak -bind_at_load -fno-exceptions -fno-rtti
tsan_x86_darwin_CXXFLAGS     = $(AM_CXXFLAGS_X86_DARWIN) -O2 -fno-weak -bind_at_load -Istlport -fno-exceptions -fno-rtti
tsan_x86_darwin_DEPENDENCIES = $(COREGRIND_LIBS_X86_DARWIN)
tsan_x86_darwin_LDADD        = $(TOOL_LDADD_X86_DARWIN)
tsan_x86_darwin_LDFLAGS      = $(TOOL_LDFLAGS_X86_DARWIN)

