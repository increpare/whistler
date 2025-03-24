SRC_DIR = src
OBJ_DIR = obj
HEADERS = $(SRC_DIR)/whistler.h

# Create object file paths
OBJS = #$(OBJ_DIR)/tinywav.o

all: $(OBJ_DIR) whistler

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# $(OBJ_DIR)/tinywav.o: $(SRC_DIR)/tinywav.c $(SRC_DIR)/tinywav.h
# 	gcc -c $(SRC_DIR)/tinywav.c -o $@

whistler: $(SRC_DIR)/whistler.c $(HEADERS)
	gcc -o $@ $(SRC_DIR)/whistler.c $(OBJS) -I/opt/homebrew/include -L/opt/homebrew/lib -lsndfile -lfftw3f -lm

clean:
	rm -f whistler
	rm -rf $(OBJ_DIR)