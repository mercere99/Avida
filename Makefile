# This is the Makefile for Avida
# To use, you must:
#  - setup EMP_DIR to indicate the location of the Empirical root directory.
#
# Build options:
#  native (default) - Optimized version of code.
#  debug - turn on all debugging options, including asserts and pointer tracking.
#  quick - no debugging or optimization; fastest compile time.
#  clean - remove all compilation artifacts including executable.
#  web - compile to WebAssembly with optimizations on (same as web-native)
#  web-debug - compile to WebAssembly with debug on (same as web-native)
#  web-quick - compile to WebAssembly with neither debug or optimization
#
# Other build options, less used:
#  grumpy - Lots of extra warnings turned on

TARGET := Avida

# Additional executables to build from source/<name>.cpp (each also gets a <name>-debug target)
ALTERNATES := DOSSIER DOSSIER-Tournament ROMEO

# Identify all directory locations
EMP_DIR      = ../Empirical
BUILD_DIR    = build
WEB_DIR      = web
SETTINGS_DIR = settings
CONFIG_DIR   = config
SOURCE_DIR   = source

# CXX = clang++
CXX_web := emcc

NATIVE_CODE = $(SOURCE_DIR)/$(TARGET).cpp
WEB_CODE = $(SOURCE_DIR)/$(TARGET)-web.cpp

NATIVE_EXE = $(BUILD_DIR)/$(TARGET)
WEB_EXE = $(WEB_DIR)/$(TARGET).js
WEB_WELL_MIXED_EXE = $(WEB_DIR)/$(TARGET)-well-mixed.js

# Specify sets of compilation flags to use
FLAGS_version := -std=c++23
FLAGS_warn    := -Wall -Wextra -Wno-unused-function -Wnon-virtual-dtor -Wcast-align -Woverloaded-virtual -pedantic
FLAGS_include := -I$(SOURCE_DIR)/ -I$(EMP_DIR)/include/
FLAGS_main    := $(FLAGS_version) $(FLAGS_warn) $(FLAGS_include) # -pthread

FLAGS_QUICK    := $(FLAGS_main) -DNDEBUG
FLAGS_DEBUG    := $(FLAGS_main) -g -DEMP_TRACK_MEM
# Not using opts:
# -flto (link-time optimization; we have only one compilation unit)
# -ffast-math (may cause issues with some floating-point calculations)
FLAGS_OPT      := $(FLAGS_main) -O3 -DNDEBUG -march=native -fno-exceptions
FLAGS_GRUMPY   := $(FLAGS_main) -DNDEBUG -Wconversion -Weffc++
FLAGS_COVERAGE := $(FLAGS_main)  -O0 -DEMP_TRACK_MEM -ftemplate-backtrace-limit=0 -fprofile-instr-generate -fcoverage-mapping -fno-inline -fno-elide-constructors

# Emscripten / Empirical information
EMP_methods  := -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap', 'UTF8ToString', 'stringToUTF8', 'lengthBytesUTF8']"
EMP_funs     := -s EXPORTED_FUNCTIONS="['_main', '_malloc', '_free', '_empCppCallback']"
EMP_js_lib   := --js-library $(EMP_DIR)/include/emp/web/library_emp.js
EMP_limits   := -s NO_EXIT_RUNTIME=1 -s INITIAL_MEMORY=268435456
EMP_warnings := -Wno-dollar-in-identifier-extension
EMP_files    := --preload-file $(CONFIG_DIR)@/config
EMP_threads  := -pthread -s PTHREAD_POOL_SIZE=1
FLAGS_emp    := $(FLAGS_main) $(EMP_methods) $(EMP_js_lib) $(EMP_funs) $(EMP_limits) $(EMP_warnings) $(EMP_files) $(EMP_threads)

FLAGS_WEB       := $(FLAGS_emp) -O3 -DNDEBUG -fno-exceptions -s DISABLE_EXCEPTION_CATCHING=1
FLAGS_WEB_DEBUG := $(FLAGS_emp) -gsource-map -pedantic -s ASSERTIONS=1
FLAGS_WEB_QUICK := $(FLAGS_emp) -DNDEBUG

native: FLAGS := $(FLAGS_OPT)
native: $(NATIVE_EXE)

default: native

debug: FLAGS := $(FLAGS_DEBUG)
debug: $(NATIVE_EXE)

grumpy: FLAGS := $(FLAGS_GRUMPY)
grumpy: $(NATIVE_EXE)

quick: FLAGS := $(FLAGS_QUICK)
quick: $(NATIVE_EXE)

web: FLAGS := $(FLAGS_WEB)
web: $(WEB_EXE)

web-debug: FLAGS := $(FLAGS_WEB_DEBUG)
web-debug: $(WEB_EXE)

web-quick: FLAGS := $(FLAGS_WEB_QUICK)
web-quick: $(WEB_EXE)

# Each profile has its own output so switching targets cannot reuse the other module pack.
web-well-mixed: FLAGS := $(FLAGS_WEB)
web-well-mixed: $(WEB_WELL_MIXED_EXE)

web-well-mixed-quick: FLAGS := $(FLAGS_WEB_QUICK)
web-well-mixed-quick: $(WEB_WELL_MIXED_EXE)

web-well-mixed-debug: FLAGS := $(FLAGS_WEB_DEBUG)
web-well-mixed-debug: $(WEB_WELL_MIXED_EXE)

all: native web

new: clean
new: native

# Debugging information
#print-%: ; @echo $*=$($*)
print-%: ; @echo '$(subst ','\'',$*=$($*))'

CLEAN_BACKUP = *~ *.dSYM
CLEAN_TEST = *.out	*.o	*.gcda	*.gcno	*.info	*.gcov	./Coverage* ./temp
WEB_ARTIFACTS = $(WEB_EXE) $(WEB_DIR)/$(TARGET).data $(WEB_DIR)/$(TARGET).wasm \
                $(WEB_DIR)/$(TARGET).wasm.map $(WEB_DIR)/$(TARGET).worker.js
WEB_ARTIFACTS += $(WEB_WELL_MIXED_EXE) $(WEB_DIR)/$(TARGET)-well-mixed.data \
                 $(WEB_DIR)/$(TARGET)-well-mixed.wasm $(WEB_DIR)/$(TARGET)-well-mixed.wasm.map \
                 $(WEB_DIR)/$(TARGET)-well-mixed.worker.js
CLEAN_EXE = $(NATIVE_EXE) $(WEB_ARTIFACTS) $(addprefix $(BUILD_DIR)/, $(ALTERNATES))

CLEAN_FILES = $(CLEAN_BACKUP) $(CLEAN_TEST) $(CLEAN_EXE)

server:
	cd $(WEB_DIR) ; python $(CURDIR)/web/serve.py

# Always run the tests, even if nothing has changed
.PHONY: web-well-mixed web-well-mixed-quick web-well-mixed-debug test-web-adapters clean debug grumpy native quick server tests web web-debug web-quick \
        test-checkpoint-diagnostics $(ALTERNATES) $(addsuffix -debug, $(ALTERNATES))

# Changes in any header file in SOURCE_DIR should trigger recompilation
KEY_HEADERS := $(shell find $(SOURCE_DIR) -name '*.hpp')
# Avida is header-only with respect to Empirical as well, so changes there must invalidate builds.
EMP_HEADERS := $(shell find $(EMP_DIR)/include/emp -name '*.hpp')

clean:
	@echo Removing:
	@echo $(wildcard $(CLEAN_FILES))
	@echo ----
	rm -rf $(wildcard $(CLEAN_FILES))

# Make sure that the needed directories exists.
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ALTERNATES): FLAGS := $(FLAGS_OPT)
$(ALTERNATES): % : $(BUILD_DIR)/%

$(addprefix $(BUILD_DIR)/, $(ALTERNATES)): $(BUILD_DIR)/% : $(SOURCE_DIR)/%.cpp Makefile $(KEY_HEADERS) $(EMP_HEADERS) | $(BUILD_DIR)
	$(CXX) $(FLAGS) $< -o $@

$(addsuffix -debug, $(ALTERNATES)): FLAGS := $(FLAGS_DEBUG)
$(addsuffix -debug, $(ALTERNATES)): %-debug : $(SOURCE_DIR)/%.cpp Makefile $(KEY_HEADERS) $(EMP_HEADERS) | $(BUILD_DIR)
	$(CXX) $(FLAGS) $< -o $(BUILD_DIR)/$*

# Compile the command-line version.
$(NATIVE_EXE): $(NATIVE_CODE) Makefile $(KEY_HEADERS) $(EMP_HEADERS) | $(BUILD_DIR)
	$(CXX) $(FLAGS) $(NATIVE_CODE) -o $(NATIVE_EXE)
	@echo To build the web version use: make web

test-checkpoint-diagnostics: $(BUILD_DIR)/checkpoint-save-load-diagnostics
	$(BUILD_DIR)/checkpoint-save-load-diagnostics

$(BUILD_DIR)/checkpoint-save-load-diagnostics: tests/checkpoint_save_load_diagnostics.cpp Makefile $(KEY_HEADERS) $(EMP_HEADERS) | $(BUILD_DIR)
	$(CXX) $(FLAGS_OPT) -DAVIDA_CHECKPOINT_DIAGNOSTICS $< -o $@

# Compile the web version.

$(WEB_EXE): $(WEB_CODE) Makefile $(KEY_HEADERS) $(EMP_HEADERS) $(shell find $(CONFIG_DIR) -type f)
	mkdir -p $(WEB_DIR)
	$(CXX_web) $(FLAGS) $(WEB_CODE) -o $(WEB_EXE)

$(WEB_WELL_MIXED_EXE): $(WEB_CODE) Makefile $(KEY_HEADERS) $(EMP_HEADERS) $(shell find $(CONFIG_DIR) -type f)
	mkdir -p $(WEB_DIR)
	$(CXX_web) $(FLAGS) -DAVIDA_WEB_POPULATION=PopWellMixed $(WEB_CODE) -o $@

test-web-adapters: $(BUILD_DIR)/web-population-adapters
	$(BUILD_DIR)/web-population-adapters

$(BUILD_DIR)/web-population-adapters: tests/web_population_adapters.cpp Makefile $(KEY_HEADERS) $(EMP_HEADERS) | $(BUILD_DIR)
	$(CXX) $(FLAGS_DEBUG) -O1 -fno-exceptions $< -o $(BUILD_DIR)/web-population-adapters
