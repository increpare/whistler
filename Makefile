SRC_DIR = src
OBJ_DIR = obj
HEADERS = 

# Create object file paths
OBJS = #$(OBJ_DIR)/tinywav.o

all: $(OBJ_DIR) whistler chorus

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# $(OBJ_DIR)/tinywav.o: $(SRC_DIR)/tinywav.c $(SRC_DIR)/tinywav.h
# 	gcc -c $(SRC_DIR)/tinywav.c -o $@

chorus: $(SRC_DIR)/chorus.c $(HEADERS)
	gcc -o $@ $(SRC_DIR)/chorus.c $(OBJS) -I/opt/homebrew/include -L/opt/homebrew/lib -lsndfile -lfftw3f -ljson-c -lm

whistler: $(SRC_DIR)/whistler.c $(HEADERS)
	gcc -o $@ $(SRC_DIR)/whistler.c $(OBJS) -I/opt/homebrew/include -L/opt/homebrew/lib -lsndfile -lfftw3f -lm

clean:
	rm -f whistler
	rm -rf $(OBJ_DIR)