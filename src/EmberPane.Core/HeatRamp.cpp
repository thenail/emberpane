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
        };

        struct Rgb
        {
            std::uint8_t R, G, B;
        };

        constexpr Stop Stops[] = {
            { 0.02, Activity::Light },
            { 0.25, Activity::Working },
            { 1.0, Activity::Busy },
            { 2.0, Activity::VeryBusy },
            { 4.0, Activity::FlatOut },
        };

        constexpr int StopCount = static_cast<int>(std::size(Stops));

        /// In the order of ColorTheme. Settings gives each stop its word, so
        /// colour is never the only signal. A window's border lies against its
        /// own content, which may be light or dark and cannot be asked, so
        /// Balanced has to work on both; the tests pin it.
        constexpr Rgb Themes[][StopCount] = {
            { { 0x4C, 0x9B, 0xE8 }, { 0x2E, 0xC4, 0xB6 }, { 0xF2, 0xC1, 0x2E }, { 0xF2, 0x71, 0x1C }, { 0xE0, 0x31, 0x31 } },
            { { 0x2F, 0x80, 0xDE }, { 0x0B, 0x92, 0x87 }, { 0xB2, 0x80, 0x00 }, { 0xE2, 0x66, 0x00 }, { 0xDC, 0x2F, 0x2F } },
            { { 0xFF, 0xD1, 0x66 }, { 0xFF, 0xB1, 0x3B }, { 0xFF, 0x8C, 0x2E }, { 0xF2, 0x54, 0x2D }, { 0xD7, 0x26, 0x3D } },
            { { 0x5E, 0xEA, 0xD4 }, { 0x38, 0xBD, 0xF8 }, { 0x81, 0x8C, 0xF8 }, { 0xC0, 0x84, 0xFC }, { 0xF4, 0x72, 0xB6 } },
        };

        /// Levels between one stop and the next: 24 in all, and each stop's
        /// own colour is one of them.
        constexpr int StepsPerSegment = 6;

        std::uint8_t Blend(std::uint8_t from, std::uint8_t to, double amount)
        {
            return static_cast<std::uint8_t>(std::lround(from + (to - from) * amount));
        }
    }

    FrameColor ColorFor(double rate, ColorTheme theme)
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

        auto const* colors = Themes[static_cast<int>(theme)];
        auto const& a = colors[index];
        auto const& b = colors[index + 1];
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
