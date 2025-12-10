#!/bin/bash

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="$PROJECT_ROOT/build"

echo "=========================================="
echo "GPU Virtual Memory - Benchmark Runner"
echo "=========================================="
echo "Project: $PROJECT_ROOT"
echo "Build Dir: $BUILD_DIR"
echo ""

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Building..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DUSE_GPU_SIMULATOR=ON -DENABLE_BENCHMARKS=ON ..
    make -j$(nproc)
else
    echo "Using existing build directory"
fi

cd "$BUILD_DIR"

echo ""
echo "Running main benchmark application..."
echo "=========================================="

if [ -f "bin/benchmark_app" ]; then
    timeout 600 ./bin/benchmark_app 2>&1 | tee benchmark_results.txt
    
    if [ -f benchmark_results.csv ]; then
        echo ""
        echo "Benchmark Results (CSV):"
        cat benchmark_results.csv
    fi
else
    echo "Error: benchmark_app not found. Please build first."
    exit 1
fi

echo ""
echo "=========================================="
echo "Running Example Applications"
echo "=========================================="

if [ -f "bin/nbody_gpu_vm" ]; then
    echo ""
    echo "Running N-Body Example (512 particles, 50 steps)..."
    timeout 120 ./bin/nbody_gpu_vm 512 50 2>&1 | tee nbody_results.txt
fi

if [ -f "bin/video_pipeline_sim" ]; then
    echo ""
    echo "Running Video Pipeline Example (100 frames, batch 4)..."
    timeout 120 ./bin/video_pipeline_sim 100 4 2>&1 | tee video_results.txt
fi

echo ""
echo "=========================================="
echo "Generating Summary Report"
echo "=========================================="

REPORT_FILE="$BUILD_DIR/benchmark_report.txt"

cat > "$REPORT_FILE" << EOF
GPU Virtual Memory Subsystem - Benchmark Report
================================================

Generated: $(date)
Build Directory: $BUILD_DIR

System Information:
- CPU: $(lscpu | grep "Model name" | cut -d: -f2 | xargs)
- Memory: $(free -h | awk '/^Mem:/ {print $2}')
- Kernel: $(uname -r)

Build Configuration:
$(cd $BUILD_DIR && cmake -L | grep -E "USE_GPU_SIMULATOR|ENABLE_" || echo "  (See CMakeCache.txt)")

Benchmark Results:
EOF

if [ -f "$BUILD_DIR/benchmark_results.csv" ]; then
    echo "" >> "$REPORT_FILE"
    echo "CSV Results:" >> "$REPORT_FILE"
    cat "$BUILD_DIR/benchmark_results.csv" >> "$REPORT_FILE"
fi

echo ""
echo "Report saved to: $REPORT_FILE"
cat "$REPORT_FILE"

echo ""
echo "=========================================="
echo "Visualization"
echo "=========================================="

VISUALIZE_SCRIPT="$PROJECT_ROOT/tools/scripts/visualize.py"
if [ -f "$VISUALIZE_SCRIPT" ] && command -v python3 &> /dev/null; then
    echo "Generating visualizations..."
    cd "$BUILD_DIR"
    python3 "$VISUALIZE_SCRIPT" benchmark_results.csv 2>/dev/null || echo "Visualization script failed (optional)"
else
    echo "Python not available or visualization script not found (optional)"
fi

echo ""
echo "=========================================="
echo "Benchmark run completed!"
echo "=========================================="
echo ""
echo "Output files:"
echo "  - benchmark_results.csv"
echo "  - benchmark_results.txt"
echo "  - benchmark_report.txt"
[ -f nbody_results.txt ] && echo "  - nbody_results.txt"
[ -f video_results.txt ] && echo "  - video_results.txt"

