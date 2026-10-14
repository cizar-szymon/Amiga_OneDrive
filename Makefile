# AmiDrive for AmigaOS 3.x (vbcc, cross compiled on Windows, run from MSYS2)
#
#   make            -> build/amiga/AmiDrive
#   make tests      -> build/amiga/AmiDriveTests (the PC test suite built with vbcc)
#   make clean
#
# Tests on the PC: make -f Makefile.host test
# Upload and run on the Amiga (from Git Bash / PowerShell, see README.md):
#   python tools/amiga_remote.py push
#   python tools/amiga_remote.py run "Work:AmiDrive/AmiDrive TEST"

AMIGA_DEV ?= C:/amiga-dev
NDK       ?= $(AMIGA_DEV)/NDK3.2/Include_H
NETINC    ?= $(AMIGA_DEV)/NDK3.2/SANA+RoadshowTCP-IP/netinclude
AMISSL    ?= $(AMIGA_DEV)/amissl/include

export VBCC := $(AMIGA_DEV)/vbcc
export PATH := $(shell cygpath -u "$(AMIGA_DEV)")/vbcc/bin:$(PATH)

# No -Rshort (bad load hunk on real hardware). -O3 is needed so vbcc drops the
# unused static inline functions of the OpenSSL headers (AmiSSL README-SDK, 11).
CFLAGS := +aos68k_win -c99 -cpu=68020 -O3 -I$(AMISSL) -I$(NETINC) -I$(NDK)

OUT  := build/amiga/AmiDrive
OBJ  := build/amiga/obj
SRCS := main.c net_amiga.c plat_amiga.c appicon_amiga.c clock_amiga.c http.c json.c fsmap.c tz.c session.c config.c auth.c \
        graph.c state.c sync.c
OBJS := $(addprefix $(OBJ)/,$(SRCS:.c=.o))
HDRS := $(wildcard src/*.h)

all: $(OUT)

$(OUT): $(OBJS)
	vc +aos68k_win $(OBJS) -o $@ -lamiga

$(OBJ)/%.o: src/%.c $(HDRS) | $(OBJ)
	vc $(CFLAGS) -c $< -o $@

$(OBJ):
	mkdir -p $@

# The PC test suite on the Amiga: big endian, 32-bit long, another compiler.
TOUT  := build/amiga/AmiDriveTests
TSRCS := src/http.c src/json.c src/fsmap.c src/tz.c src/session.c src/config.c src/auth.c \
         src/graph.c src/state.c src/sync.c tests/tests.c tests/test_http.c tests/test_json.c tests/test_fsmap.c \
         tests/test_tz.c tests/test_session.c tests/test_config.c tests/test_auth.c tests/test_graph.c tests/test_state.c tests/test_sync.c \
         tests/fake_net.c tests/fake_plat.c

tests: $(TOUT)

$(TOUT): $(TSRCS) $(HDRS) tests/tests.h | $(OBJ)
	vc $(CFLAGS) -Isrc $(TSRCS) -o $@

clean:
	rm -rf build/amiga

.PHONY: all tests clean
