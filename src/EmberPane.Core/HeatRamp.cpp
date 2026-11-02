#include "pch.h"
#include "HeatRamp.h"

namespace EmberPane::Core
{
    namespace
    {
        struct Stop
        {
            double Rate;
            Activity Word;
            std::uint8_t R, G, B;
        };

        /// Blue to amber to red avoids relying on telling red from green, and
        /// Settings gives each its word, so colour is never the only signal.
        /// A window's border lies against its own content, which may be light
        /// or dark and cannot be asked, so these are mid-tones: every level is
        /// at least 3:1 against both, as the tests pin. Brighter ones read
        /// better on dark windows and vanish on white ones.
        constexpr Stop Stops[] = {
            { 0.02, Activity::Light, 0x2F, 0x80, 0xDE },
            { 0.25, Activity::Working, 0x0B, 0x92, 0x87 },
            { 1.0, Activity::Busy, 0xB2, 0x80, 0x00 },
            { 2.0, Activity::VeryBusy, 0xE2, 0x66, 0x00 },
            { 4.0, Activity::FlatOut, 0xDC, 0x2F, 0x2F },
        };

        constexpr int StopCount = static_cast<int>(std::size(Stops));

        /// Levels between one stop and the next: 24 in all, and each stop's
        /// own colour is one of them.
        constexpr int StepsPerSegment = 6;

        std::uint8_t Blend(std::uint8_t from, std::uint8_t to, double amount)
        {
            return static_cast<std::uint8_t>(std::lround(from + (to - from) * amount));
        }
    }

    FrameColor ColorFor(double rate)
    {
        if (rate < Stops[0].Rate) return {};

        double position = StopCount - 1;
        for (int i = 0; i + 1 < StopCount; ++i)
        {
            if (rate < Stops[i + 1].Rate)
            {
                position = i + std::log2(rate / Stops[i].Rate) / std::log2(Stops[i + 1].Rate / Stops[i].Rate);
                break;
            }
        }

        double level = std::round(position * StepsPerSegment) / StepsPerSegment;
        int index = std::min(static_cast<int>(level), StopCount - 2);
        double amount = level - index;

        auto const& a = Stops[index];
        auto const& b = Stops[index + 1];
        return { false, Blend(a.R, b.R, amount), Blend(a.G, b.G, amount), Blend(a.B, b.B, amount) };
    }

    Activity ActivityFor(double rate)
    {
        auto activity = Activity::Resting;
        for (auto const& stop : Stops)
            if (rate >= stop.Rate) activity = stop.Word;
        return activity;
    }
}
