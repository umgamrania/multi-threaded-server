# --- Project layout ---
SRC_DIR   := src
INC_DIR   := include
BIN_DIR   := bin
OBJ_DIR   := $(BIN_DIR)/obj

TARGET    := $(BIN_DIR)/server

# --- Toolchain ---
CC        := gcc
CFLAGS    := -std=c11 -Wall -Wextra -I$(INC_DIR)
LDFLAGS   :=

# Build type: `make` for release, `make DEBUG=1` for a debug build
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2
endif

# --- Sources / objects ---
SRCS      := $(wildcard $(SRC_DIR)/*.c)
OBJS      := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS      := $(OBJS:.o=.d)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)
