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

        /// Windows draws no border at all.
        bool None{ false };

        bool operator==(FrameColor const&) const = default;
    };

    /// The line that can lie between a window and its colour, so that the
    /// colour stands clear of a light window.
    inline constexpr FrameColor DarkLine{ false, 0x1F, 0x1F, 0x1F };

    inline constexpr FrameColor NoBorder{ false, 0, 0, 0, true };

    enum class Activity
    {
        Resting,
        Light,
        Working,
        Busy,
        VeryBusy,
        FlatOut,
    };

    /// The sets of colours on offer in Settings. Each has the same stops.
    enum class ColorTheme
    {
        /// Blue through amber to red. Vivid, and pale against a light window
        /// unless a dark line stands between them.
        Bright,

        /// The same hues in mid-tones: every level is at least 3:1 against
        /// light and dark windows alike.
        Balanced,

        /// All fire: gold through orange to crimson.
        Ember,

        /// Cool to vivid: aqua through indigo to pink.
        Aurora,
    };

    /// The colour blends between the stops, on log2(rate), and is quantized:
    /// a window's colour changes only when its load really has, so the common
    /// tick makes no DWM call.
    FrameColor ColorFor(double rate, ColorTheme theme = ColorTheme::Bright);

    /// The word changes exactly at each stop.
    Activity ActivityFor(double rate);
}
