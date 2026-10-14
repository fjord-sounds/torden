#pragma once
#include <fjord_base/core/Dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace torden::dsp
{
    using namespace fjord::base::dsp;   // softClip, Random, Smoother, Svf, polyBlep, panGains, DelayLine, Biquad ...

    constexpr int kNumLayers = 4;        // body, click, sub, noise
    constexpr int kMaxVoices = 4;        // a kick is monophonic; extra voices only fade out or ring on (overlap mode)

}
