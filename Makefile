# InterviewPrep build commands
#
#   make cli        command-line tool, build/interviewprep
#   make test       rule tests under memory and undefined-behavior checks, then the MCP server
#   make sim        run made-up learners through the queue, to see what a rule change does
#   make resources  data/resources.json from the labels and decks
#   make wasm       the C++ core for the website, web/core.wasm
#   make web        the website, one self-contained file, dist/interviewprep.html
#   make mac        the Mac app, dist/InterviewPrep.app
#   make dmg        a disk image to hand to other people, dist/InterviewPrep.dmg
#   make all        everything above

CXX      ?= clang++
# The code says `using namespace std;` and calls move() without std::, which
# clang warns about on every call. NO_MOVE_WARNING turns just that warning off.
NO_MOVE_WARNING := -Wno-unqualified-std-cast-call
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra $(NO_MOVE_WARNING)
WASI_SDK ?= .tools/wasi-sdk

CORE_SRC := core/json.cpp core/schedule.cpp core/resources.cpp core/profile.cpp \
            core/recommend.cpp core/engine.cpp core/coach.cpp
CORE_HDR := $(wildcard core/*.hpp) core/capi.h
CLI_SRC  := cli/main.cpp cli/mcp.cpp
CLI_HDR  := $(wildcard cli/*.hpp)

.PHONY: all cli test sim resources wasm web mac dmg clean

all: test cli resources web mac

cli: build/interviewprep

build/interviewprep: $(CORE_SRC) $(CLI_SRC) $(CORE_HDR) $(CLI_HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(CORE_SRC) $(CLI_SRC) -o $@

build/test_engine: $(CORE_SRC) tests/test_engine.cpp $(CORE_HDR)
	@mkdir -p build
	$(CXX) -std=c++17 -g -O1 -Wall -Wextra $(NO_MOVE_WARNING) -fsanitize=address,undefined \
	    $(CORE_SRC) tests/test_engine.cpp -o $@

test: build/test_engine build/interviewprep
	./build/test_engine
	python3 -I tests/test_mcp.py

build/sim_recommender: $(CORE_SRC) tests/sim_recommender.cpp $(CORE_HDR)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(CORE_SRC) tests/sim_recommender.cpp -o $@

sim: build/sim_recommender data/resources.json
	./build/sim_recommender

resources: data/resources.json

data/resources.json: scripts/build_resources.py $(wildcard data/decks/*.json) data/curated.json $(wildcard data/problem_notes.json) \
                    $(wildcard data/topics.json data/topic_labels.json)
	python3 -I scripts/build_resources.py

# The core for the browser: a reactor module with no JavaScript glue, so the
# same seven functions in capi.h are all the page ever sees.
wasm: web/core.wasm

web/core.wasm: $(CORE_SRC) core/capi.cpp $(CORE_HDR)
	@test -x $(WASI_SDK)/bin/clang++ || { echo "The WebAssembly compiler is missing. Run scripts/get_toolchain.sh once, then try again."; exit 1; }
	$(WASI_SDK)/bin/clang++ --target=wasm32-wasip1 --sysroot=$(WASI_SDK)/share/wasi-sysroot \
	    -std=c++17 -O2 $(NO_MOVE_WARNING) -fno-exceptions -fno-rtti -flto -mexec-model=reactor \
	    -Wl,--strip-all -Wl,--gc-sections -Wl,-z,stack-size=1048576 -Wl,--max-memory=268435456 \
	    $(CORE_SRC) core/capi.cpp -o $@

web: dist/interviewprep.html

dist/interviewprep.html: web/index.html web/style.css web/app.js web/core.wasm data/resources.json scripts/build_web.py
	@mkdir -p dist
	python3 -I scripts/build_web.py

mac: resources
	./scripts/build_mac.sh

dmg: mac
	./scripts/build_dmg.sh

clean:
	rm -rf build dist mac/.build web/core.wasm
