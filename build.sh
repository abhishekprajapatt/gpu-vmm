#!/bin/bash

set -e

PROJECT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$PROJECT_DIR/build"

echo "=========================================="
echo "GPU Virtual Memory - Build Script"
echo "=========================================="
echo ""

if [ "$1" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    exit 0
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

echo "Configuring CMake (simulator mode, tests enabled)..."
cmake -DUSE_GPU_SIMULATOR=ON \
       -DENABLE_TESTS=ON \
       -DENABLE_BENCHMARKS=ON \
       -DENABLE_EXAMPLES=ON \
       -DCMAKE_BUILD_TYPE=Release \
       .. > /dev/null

echo "Building..."
NPROC=$(nproc 2>/dev/null || echo 4)
make -j"$NPROC" 2>&1 | tail -20

echo ""
echo "=========================================="
echo "Build Complete!"
echo "=========================================="
echo ""
echo "Executables:"
echo "  - bin/vm_tests              (Unit tests)"
echo "  - bin/benchmark_app         (Microbenchmarks)"
echo "  - bin/nbody_gpu_vm          (N-Body example)"
echo "  - bin/video_pipeline_sim    (Video pipeline example)"
echo ""
echo "Next steps:"
echo "  Run tests:     cd build && ./bin/vm_tests"
echo "  Run benchmark: cd build && ./bin/benchmark_app"
echo "  Run examples:  cd build && ./bin/nbody_gpu_vm"
echo ""
