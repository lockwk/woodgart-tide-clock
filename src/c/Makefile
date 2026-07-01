# =============================================================================
# Makefile for the 13.3" tide clock (13.3inch e-Paper HAT K, 960x680, 4-gray)
#
# Usage (on the Pi, from ~/tide-clock-dev/src/c/):
#
#   make clean && make RPI        <- build the tide_clock binary
#   make fidelity_test            <- build the Phase 3.5 visual fidelity test
#   sudo ./epd                    <- run the tide clock
#
# EPD=epd13in3k is accepted as a make variable but not used -- only one
# display is supported, so the driver is always EPD_13in3k.c.
# =============================================================================

# Waveshare library root (never modify; used read-only)
LIB := /home/pi/e-Paper/RaspberryPi_JetsonNano/c/lib

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 \
            -I. \
            -I$(LIB) \
            -I$(LIB)/e-Paper \
            -I$(LIB)/Config
PI_DEFS := -D USE_LGPIO_LIB -D RPI
LDFLAGS := -lm
PI_LIBS := -llgpio

# Waveshare driver sources (never modify)
EPD_SRC := $(LIB)/e-Paper/EPD_13in3k.c
CFG_SRC := $(LIB)/Config/DEV_Config.c
SPI_SRC := $(LIB)/Config/dev_hardware_SPI.c
WS_SRCS := $(EPD_SRC) $(CFG_SRC) $(SPI_SRC)

# Project sources
# Add new font .c files here as each phase generates them.
PROJ_SRCS := tide_clock.c \
             json_reader.c \
             layout.c \
             tide_curve.c \
             inter_font.c \
             fonts/inter_b_14.c \
             fonts/inter_lt_40.c

TARGET := epd

# -----------------------------------------------------------------------------
# Primary targets
# -----------------------------------------------------------------------------

.PHONY: all RPI clean fidelity_test epd_clear

# `make RPI` is the standard build command documented in the plan.
# It's an alias for `all` -- the Pi-specific flags are always included.
RPI: all

all: $(TARGET)

$(TARGET): $(PROJ_SRCS) $(WS_SRCS)
	$(CC) $(CFLAGS) $(PI_DEFS) $^ $(PI_LIBS) $(LDFLAGS) -o $@

# Phase 3.5 visual fidelity test (standalone -- no json_reader or layout)
FIDELITY_SRCS := fidelity_test.c \
                 inter_font.c \
                 fonts/inter_lt_40.c \
                 fonts/inter_b_14.c \
                 $(WS_SRCS)

fidelity_test: $(FIDELITY_SRCS)
	$(CC) $(CFLAGS) $(PI_DEFS) $^ $(PI_LIBS) $(LDFLAGS) -o $@

# Burn-in clearing utility (cycles black/white to clear ghosting)
epd_clear: epd_clear.c $(WS_SRCS)
	$(CC) $(CFLAGS) $(PI_DEFS) $^ $(PI_LIBS) $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET) fidelity_test epd_clear *.o
