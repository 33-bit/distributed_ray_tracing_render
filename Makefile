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

# --- OpenMP (for the MPI+OpenMP hybrid build) ---
# Apple clang needs libomp + -Xpreprocessor; Linux/gcc takes plain -fopenmp.
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  LIBOMP  := $(shell brew --prefix libomp 2>/dev/null)
  OMP_CXX := -Xpreprocessor -fopenmp -I$(LIBOMP)/include
  OMP_LD  := -L$(LIBOMP)/lib -lomp
else
  OMP_CXX := -fopenmp
  OMP_LD  := -fopenmp
endif

MAIN := src/main.cpp
BUILD := build

.PHONY: all seq mpi test clean
all: seq mpi

# Sequential baseline. Depends on every header so edits trigger rebuild.
seq: $(MAIN) $(wildcard src/**/*.hpp)
	$(CXX) $(CXXFLAGS) $(MAIN) -o raytracer_seq $(LDFLAGS)

# Distributed build. USE_MPI pulls in src/mpi/; OpenMP threads each worker
# across its cores (the MPI+OpenMP hybrid).
mpi: $(MAIN) $(wildcard src/**/*.hpp)
	$(MPICXX) $(CXXFLAGS) -DUSE_MPI $(OMP_CXX) $(MAIN) -o raytracer_mpi $(LDFLAGS) $(OMP_LD)

# Core unit tests grow as Members A and B land their modules.
test: src/test_core.cpp $(wildcard src/**/*.hpp)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) src/test_core.cpp -o $(BUILD)/test_core
	./$(BUILD)/test_core

clean:
	rm -f raytracer_seq raytracer_mpi
	rm -rf $(BUILD)
