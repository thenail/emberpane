#pragma once

#include <cstdint>

/// How hard an app is working, for people: a colour for its frame and a word
/// for Settings. The scale is absolute, in cores' worth of CPU time. As a
/// share of the machine, one pegged core would read as 3% on a 32-thread CPU.
namespace EmberPane::Core
{
    struct FrameColor
    {
        /// Below the first stop: the window keeps the border Windows gives it.
        bool UseSystemDefault{ true };
        std::uint8_t R{ 0 };
        std::uint8_t G{ 0 };
        std::uint8_t B{ 0 };

        bool operator==(FrameColor const&) const = default;
    };

    enum class Activity
    {
        Resting,
        Light,
        Working,
        Busy,
        VeryBusy,
        FlatOut,
    };

    /// The colour blends between the stops, on log2(rate), and is quantized:
    /// a window's colour changes only when its load really has, so the common
    /// tick makes no DWM call.
    FrameColor ColorFor(double rate);

    /// The word changes exactly at each stop.
    Activity ActivityFor(double rate);
}
