CC=gcc
LDFLAGS=-lportaudio -lpthread

OBJ_DIR=obj
BIN_DIR=bin
BIN_NAME=player

.PHONY = all clean distclean

$(BIN_DIR)/(BIN_NAME) : $(OBJ_DIR)/$(BIN_NAME).o
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$(BIN_NAME) $(LDFLAGS)

$(OBJ_DIR)/$(BIN_NAME).o : $(BIN_NAME).c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $(OBJ_DIR)/$(BIN_NAME).o


all : $(BIN_DIR)/$(BIN_NAME)

clean:
	rm -rf $(OBJ_DIR)

distclean: clean
	rm -rf $(BIN_DIR)
