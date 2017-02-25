CC=gcc

CFLAGS= -Wall -Wextra -Werror
LDFLAGS=-lportaudio -lpthread

SRC_DIR=src
OBJ_DIR=obj
BIN_DIR=bin
BIN_NAME=player

C_FILES := $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES := $(addprefix $(OBJ_DIR)/,$(notdir $(C_FILES:.c=.o)))

.PHONY = all clean distclean

$(BIN_DIR)/$(BIN_NAME) : $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^


all : $(BIN_DIR)/$(BIN_NAME)

clean:
	rm -rf $(OBJ_DIR)

distclean: clean
	rm -rf $(BIN_DIR)
