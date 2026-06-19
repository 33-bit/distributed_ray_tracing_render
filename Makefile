# MPI Distributed Ray Tracer — build system
# Two targets from one codebase:
#   make seq   -> raytracer_seq  (plain clang++, correctness baseline, no MPI)
#   make mpi   -> raytracer_mpi  (mpic++, -DUSE_MPI, master-worker layer)
#   make test  -> build + run the core unit tests (Members A & B)
#   make clean

CXX      ?= clang++
MPICXX   ?= mpic++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Isrc
LDFLAGS  ?=

MAIN := src/main.cpp
BUILD := build

.PHONY: all seq mpi test clean
all: seq mpi

# Sequential baseline. Depends on every header so edits trigger rebuild.
seq: $(MAIN) $(wildcard src/**/*.hpp)
	$(CXX) $(CXXFLAGS) $(MAIN) -o raytracer_seq $(LDFLAGS)

# Distributed build. USE_MPI pulls in everything under src/mpi/.
mpi: $(MAIN) $(wildcard src/**/*.hpp)
	$(MPICXX) $(CXXFLAGS) -DUSE_MPI $(MAIN) -o raytracer_mpi $(LDFLAGS)

# Core unit tests grow as Members A and B land their modules.
test: src/test_core.cpp $(wildcard src/**/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) src/test_core.cpp -o $(BUILD)/test_core
	./$(BUILD)/test_core

clean:
	rm -f raytracer_seq raytracer_mpi
	rm -rf $(BUILD)
