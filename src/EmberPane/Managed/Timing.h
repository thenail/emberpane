#pragma once

namespace EmberPane::Managed
{
    /// How often the host samples and reconciles. Slow enough to cost nothing,
    /// quick enough that a frame leaves idle within two ticks of a load.
    inline constexpr int SampleSeconds = 2;
}
