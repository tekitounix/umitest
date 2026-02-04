#include <cstdio>

#include <umi/bench/bench.hh>

int main() {
    umi::bench::TimerImpl::enable();
    umi::bench::PlatformInlineRunner runner;
    runner.calibrate<64>();

    auto stats = runner.benchmark_corrected<64>([] {
        volatile int value = 0;
        value += 1;
        (void)value;
    });

    if (stats.max < stats.min || stats.mean < stats.min) {
        std::printf("bench: failed\n");
        return 1;
    }

    std::printf("bench: ok\n");
    return 0;
}
