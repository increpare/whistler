SRC_DIR = src
OBJ_DIR = obj
HEADERS = 
WEB_DIR = web

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

# WebAssembly targets
wasm: web-dir whistler-web

web-dir:
	mkdir -p $(WEB_DIR)

# Using a simplified approach for WebAssembly version
whistler-web: $(SRC_DIR)/whistler_web.c $(HEADERS)
	emcc $(SRC_DIR)/whistler_web.c -o $(WEB_DIR)/whistler.js \
		-s WASM=1 \
		-s EXPORTED_FUNCTIONS='["_main", "_process_audio", "_malloc", "_free", "_get_instrument_count", "_get_instrument_name"]' \
		-s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap", "getValue", "setValue", "UTF8ToString"]' \
		-s ALLOW_MEMORY_GROWTH=1 \
		-s ASSERTIONS=1 \
		-s NO_EXIT_RUNTIME=1 \
		-s ENVIRONMENT='web' \
		-s MODULARIZE=1 \
		-s EXPORT_ES6=0 \
		-s EXPORT_NAME='WhistlerModule' \
		-O2

clean:
	rm -f whistler
	rm -rf $(OBJ_DIR)
	rm -rf $(WEB_DIR)/whistler.js $(WEB_DIR)/whistler.wasm $(WEB_DIR)/whistler.data

setup-web: web-dir
	mkdir -p $(WEB_DIR)/css $(WEB_DIR)/js
	touch $(WEB_DIR)/shell.html $(WEB_DIR)/css/style.css $(WEB_DIR)/js/app.js

# Run a simple web server for testing
serve:
	cd $(WEB_DIR) && python3 -m http.server 8080