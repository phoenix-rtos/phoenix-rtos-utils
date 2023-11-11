#
# Makefile for Phoenix-RTOS ld.elf_so
#
# Copyright 2023 Phoenix Systems
#

# - NAME - component/target binary name
# - LOCAL_SRCS - list of source files relative to current makefile
# - SRCS       - list of source files relative to project root
# - LOCAL_HEADERS     - headers to be installed (relative to current makefile)
# - LOCAL_HEADERS_DIR - headers tree be installed (relative to current makefile) - default "include"
# - HEADERS           - headers to be installed (relative to project root)
# - DEP_LIBS - static libraries from current repo needed to be compiled/installed before this component (shortcut for putting something in LIBS and DEPS)
# - DEPS - list of components from current repo to be completed before starting this one
# - LIBS - names of the static libs to link the binary against (without .a suffix)
# - LOCAL_CFLAGS  - additional CFLAGS for current component compilation
# - LOCAL_CXXFLAGS - additional CXXFLAGS for current component compilation
# - LOCAL_LDFLAGS - additional LDFLAGS for current component linking
# - LOCAL_LDLIBS  - additional LDLIBS for current component linking
# - LOCAL_INSTALL_PATH - custom rootfs dir for the binary to be installed (if not provided - DEFAULT_INSTALL_PATH)

NAME := ld.elf_so
LOCAL_SRCS := compat.c debug.c diagassert.c expand.c hash.c headers.c load.c map_object.c paths.c reloc.c rtld.c search.c symbol.c symver.c tls.c xmalloc.c xprintf.c
LIBS := libphoenix

# .if ${LDELFSO_MACHINE_ARCH} == "i386" || ${LDELFSO_MACHINE_ARCH} == "x86_64"
# COPTS+=		-mno-3dnow -mno-mmx -mno-sse -mno-sse2 -mno-sse3
# .endif
COPTS := -mno-3dnow -mno-mmx -mno-sse -mno-sse2 -mno-sse3

COPTS += -fvisibility=hidden

# rtld.c and symbol.c use alloca, so disable SSP warnings.
COPTS.rtld.c+=	-Wno-stack-protector
COPTS.symbol.c+=-Wno-stack-protector

include $(binary.mk)