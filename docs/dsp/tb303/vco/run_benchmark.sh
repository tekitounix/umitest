#!/bin/bash
# =============================================================================
# TB-303 WaveShaper Benchmark Suite
# =============================================================================
#
# 使用方法:
#   ./run_benchmark.sh [options]
#
# オプション:
#   --build-only    ビルドのみ
#   --renode-only   Renodeベンチマークのみ
#   --python-only   Python精度検証のみ
#   --plot-only     グラフ生成のみ
#   --all           全て実行（デフォルト）
#
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}======================================${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

# =============================================================================
# Step 1: Build
# =============================================================================
do_build() {
    print_header "Step 1: Building bench_waveshaper"
    cd "$PROJECT_ROOT"

    if xmake build bench_waveshaper 2>&1; then
        print_success "Build successful"
    else
        print_error "Build failed"
        exit 1
    fi
}

# =============================================================================
# Step 2: Run Renode Benchmark (Cortex-M4 cycle-accurate)
# =============================================================================
do_renode() {
    print_header "Step 2: Running Renode Benchmark (Cortex-M4)"
    cd "$PROJECT_ROOT"

    if ! command -v renode &> /dev/null; then
        print_warning "Renode not found, skipping hardware benchmark"
        return 0
    fi

    echo "Running emulation (this takes ~60 seconds)..."
    if timeout 120 renode --console --disable-xwt tools/renode/bench_waveshaper.resc 2>&1 | head -30; then
        print_success "Renode benchmark complete"
    else
        print_warning "Renode benchmark timed out or failed"
    fi

    # Display results
    if [ -f "$BUILD_DIR/bench_waveshaper_uart.log" ]; then
        echo ""
        print_header "Benchmark Results (Cortex-M4 @ 168MHz)"
        cat "$BUILD_DIR/bench_waveshaper_uart.log"
    fi
}

# =============================================================================
# Step 3: Python Accuracy Comparison
# =============================================================================
do_python() {
    print_header "Step 3: Python Accuracy Comparison"
    cd "$SCRIPT_DIR/test"

    if ! command -v python3 &> /dev/null; then
        print_warning "Python3 not found, skipping accuracy test"
        return 0
    fi

    if python3 compare_cpp_python.py 2>&1; then
        print_success "Python accuracy test complete"
    else
        print_warning "Python test failed (matplotlib may be missing)"
    fi
}

# =============================================================================
# Step 4: Generate Plots
# =============================================================================
do_plot() {
    print_header "Step 4: Generating Plots"
    cd "$PROJECT_ROOT"

    if ! command -v python3 &> /dev/null; then
        print_warning "Python3 not found, skipping plot generation"
        return 0
    fi

    # Check if matplotlib is available
    if ! python3 -c "import matplotlib" 2>/dev/null; then
        print_warning "matplotlib not installed, skipping plots"
        return 0
    fi

    if [ -f "tools/python/bench_waveshaper_plot.py" ]; then
        python3 tools/python/bench_waveshaper_plot.py 2>&1 || true
        print_success "Plots generated in build/"
    fi
}

# =============================================================================
# Step 5: Summary
# =============================================================================
do_summary() {
    print_header "Summary"

    echo ""
    echo "Generated files:"

    if [ -f "$BUILD_DIR/bench_waveshaper_uart.log" ]; then
        echo "  - build/bench_waveshaper_uart.log (Cortex-M4 benchmark)"
    fi

    if [ -f "$SCRIPT_DIR/test/schur_lambertw_comparison.png" ]; then
        echo "  - docs/dsp/tb303/vco/test/schur_lambertw_comparison.png"
    fi

    if [ -f "$BUILD_DIR/waveshaper_benchmark.png" ]; then
        echo "  - build/waveshaper_benchmark.png"
    fi

    if [ -f "$BUILD_DIR/omega_comparison.png" ]; then
        echo "  - build/omega_comparison.png"
    fi

    echo ""

    # Extract key metrics
    if [ -f "$BUILD_DIR/bench_waveshaper_uart.log" ]; then
        echo "Key Performance Metrics (Cortex-M4 @ 168MHz, 48kHz):"
        echo "=================================================="
        grep -E "^(WaveShaperSchur:|SchurLambertW:|SchurOmega3:|Decoupled:)" "$BUILD_DIR/bench_waveshaper_uart.log" | head -10
        echo ""
        echo "Speedup vs Baseline (364 cycles):"
        grep -A5 "Speedup vs Baseline" "$BUILD_DIR/bench_waveshaper_uart.log" | tail -5
    fi

    print_success "Benchmark suite complete!"
}

# =============================================================================
# Main
# =============================================================================
main() {
    local build=true
    local renode=true
    local python=true
    local plot=true

    # Parse arguments
    for arg in "$@"; do
        case $arg in
            --build-only)
                renode=false; python=false; plot=false ;;
            --renode-only)
                build=false; python=false; plot=false ;;
            --python-only)
                build=false; renode=false; plot=false ;;
            --plot-only)
                build=false; renode=false; python=false ;;
            --all)
                ;; # defaults
            --help|-h)
                echo "Usage: $0 [--build-only|--renode-only|--python-only|--plot-only|--all]"
                exit 0 ;;
            *)
                echo "Unknown option: $arg"
                exit 1 ;;
        esac
    done

    print_header "TB-303 WaveShaper Benchmark Suite"
    echo "Project root: $PROJECT_ROOT"
    echo ""

    [ "$build" = true ] && do_build
    [ "$renode" = true ] && do_renode
    [ "$python" = true ] && do_python
    [ "$plot" = true ] && do_plot

    do_summary
}

main "$@"
