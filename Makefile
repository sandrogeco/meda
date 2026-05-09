CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

# Use pkg-config if available, otherwise hardcode paths
MODBUS_CFLAGS := $(shell pkg-config --cflags libmodbus 2>/dev/null || echo "")
MODBUS_LIBS   := $(shell pkg-config --libs libmodbus 2>/dev/null || echo "-lmodbus")

CFLAGS  += $(MODBUS_CFLAGS) -Ilibmseed -Ilibdali
LDFLAGS += $(MODBUS_LIBS) -lcjson

# Static libraries for libmseed and libdali
LIBMSEED_A := libmseed/libmseed.a
LIBDALI_A  := libdali/libdali.a

SRCDIR  := src
OBJDIR  := build

MEDA_SRCS   := $(SRCDIR)/main.c $(SRCDIR)/config.c $(SRCDIR)/scheduler.c $(SRCDIR)/mseed.c
RSTILT_SRCS := $(SRCDIR)/rstilt_main.c $(SRCDIR)/rstilt_config.c \
               $(SRCDIR)/rstilt_parser.c $(SRCDIR)/rstilt_parser_thk.c \
               $(SRCDIR)/config.c $(SRCDIR)/mseed.c

MEDA_OBJS   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/meda/%.o,$(MEDA_SRCS))
RSTILT_OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/rstilt/%.o,$(RSTILT_SRCS))

.PHONY: all clean modsim

all: meda rstilt modsim

$(LIBMSEED_A):
	$(MAKE) -C libmseed static CC="$(CC)"

$(LIBDALI_A):
	$(MAKE) -C libdali static CC="$(CC)"

meda: $(MEDA_OBJS) $(LIBMSEED_A) $(LIBDALI_A)
	$(CC) $(CFLAGS) -o $@ $(MEDA_OBJS) $(LIBMSEED_A) $(LIBDALI_A) $(LDFLAGS)

rstilt: $(RSTILT_OBJS) $(LIBMSEED_A) $(LIBDALI_A)
	$(CC) $(CFLAGS) -o $@ $(RSTILT_OBJS) $(LIBMSEED_A) $(LIBDALI_A) $(LDFLAGS)

$(OBJDIR)/meda/%.o: $(SRCDIR)/%.c | $(OBJDIR)/meda
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/rstilt/%.o: $(SRCDIR)/%.c | $(OBJDIR)/rstilt
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/meda $(OBJDIR)/rstilt:
	mkdir -p $@

modsim: tools/modsim.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -rf $(OBJDIR) $(TARGET) modsim
	$(MAKE) -C libmseed clean
	$(MAKE) -C libdali clean

# Cross-compile for RUT956 (MIPS mipsel_24kc musl)
SDK         := $(CURDIR)/rutos-ramips-rut9m-sdk
TOOLCHAIN   := $(SDK)/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl
SYSROOT     := $(SDK)/staging_dir/target-mipsel_24kc_musl
CROSS_CC    := $(TOOLCHAIN)/bin/mipsel-openwrt-linux-musl-gcc
CROSS_CFLAGS := -Wall -Wextra -O2 --sysroot=$(SYSROOT) \
                -I$(SYSROOT)/usr/include \
                -Ilibmseed -Ilibdali -DRUTOS_SDK
CROSS_LDFLAGS := -L$(SYSROOT)/usr/lib -lmodbus -lcjson
RUT_OBJDIR  := build-rut/obj
RUT_LIBMSEED := build-rut/libmseed.a
RUT_LIBDALI  := build-rut/libdali.a
RUT_MEDA_OBJS   := $(patsubst $(SRCDIR)/%.c,$(RUT_OBJDIR)/meda/%.o,$(MEDA_SRCS))
RUT_RSTILT_OBJS := $(patsubst $(SRCDIR)/%.c,$(RUT_OBJDIR)/rstilt/%.o,$(RSTILT_SRCS))

LIBMSEED_SRCS := $(wildcard libmseed/*.c)
LIBDALI_SRCS  := $(wildcard libdali/*.c)
CJSON_SRC     := $(SDK)/build_dir/target-mipsel_24kc_musl/cJSON-1.7.18/cJSON.c
RUT_MSEED_OBJS := $(patsubst libmseed/%.c,$(RUT_OBJDIR)/mseed_%.o,$(LIBMSEED_SRCS))
RUT_DALI_OBJS  := $(patsubst libdali/%.c,$(RUT_OBJDIR)/dali_%.o,$(LIBDALI_SRCS))
RUT_CJSON_OBJ  := $(RUT_OBJDIR)/cjson.o

.PHONY: rut rut-clean

rut: export STAGING_DIR=$(SDK)/staging_dir
rut: $(RUT_OBJDIR) $(RUT_LIBMSEED) $(RUT_LIBDALI) $(RUT_CJSON_OBJ) build-rut/meda build-rut/rstilt build-rut/modsim

$(RUT_OBJDIR)/mseed_%.o: libmseed/%.c | $(RUT_OBJDIR)
	$(CROSS_CC) $(CROSS_CFLAGS) -Ilibmseed -c -o $@ $<

$(RUT_OBJDIR)/dali_%.o: libdali/%.c | $(RUT_OBJDIR)
	$(CROSS_CC) $(CROSS_CFLAGS) -Ilibdali -c -o $@ $<

$(RUT_LIBMSEED): $(RUT_MSEED_OBJS)
	$(TOOLCHAIN)/bin/mipsel-openwrt-linux-musl-ar rcs $@ $^

$(RUT_LIBDALI): $(RUT_DALI_OBJS)
	$(TOOLCHAIN)/bin/mipsel-openwrt-linux-musl-ar rcs $@ $^

$(RUT_OBJDIR)/meda $(RUT_OBJDIR)/rstilt:
	mkdir -p $@

$(RUT_OBJDIR)/meda/%.o: $(SRCDIR)/%.c | $(RUT_OBJDIR)/meda
	$(CROSS_CC) $(CROSS_CFLAGS) -c -o $@ $<

$(RUT_OBJDIR)/rstilt/%.o: $(SRCDIR)/%.c | $(RUT_OBJDIR)/rstilt
	$(CROSS_CC) $(CROSS_CFLAGS) -c -o $@ $<

$(RUT_CJSON_OBJ): $(CJSON_SRC) | $(RUT_OBJDIR)
	$(CROSS_CC) $(CROSS_CFLAGS) -w -c -o $@ $<

build-rut/meda: $(RUT_MEDA_OBJS) $(RUT_LIBMSEED) $(RUT_LIBDALI) $(RUT_CJSON_OBJ)
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $(RUT_MEDA_OBJS) $(RUT_LIBMSEED) $(RUT_LIBDALI) $(RUT_CJSON_OBJ) -L$(SYSROOT)/usr/lib -lmodbus

build-rut/rstilt: $(RUT_RSTILT_OBJS) $(RUT_LIBMSEED) $(RUT_LIBDALI) $(RUT_CJSON_OBJ)
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $(RUT_RSTILT_OBJS) $(RUT_LIBMSEED) $(RUT_LIBDALI) $(RUT_CJSON_OBJ)

build-rut/modsim: tools/modsim.c
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $< -lm

rut-clean:
	rm -rf $(RUT_OBJDIR) build-rut/meda build-rut/rstilt build-rut/modsim build-rut/ringserver

# Cross-compile ringserver for RUT956
.PHONY: rut-ring rut-ring-clean

RING_CC := $(CROSS_CC) --sysroot=$(SYSROOT)

rut-ring: export STAGING_DIR=$(SDK)/staging_dir
rut-ring:
	$(MAKE) -C ringserver CC="$(RING_CC)" LDFLAGS="-latomic"
	cp ringserver/ringserver build-rut/ringserver

rut-ring-clean:
	$(MAKE) -C ringserver clean
	rm -f build-rut/ringserver
