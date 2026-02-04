#pragma once

#include <umi/bench/timer.hh>

#include "baseline.hh"
#include "runner.hh"
#include "stats.hh"
#include "timer_concept.hh"

namespace umi::bench {

using PlatformRunner = Runner<TimerImpl>;
using PlatformInlineRunner = InlineRunner<TimerImpl>;

} // namespace umi::bench
