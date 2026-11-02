#include "Test.h"
#include "HeatRamp.h"
#include <algorithm>
#include <cmath>

using namespace EmberPane::Core;

namespace
{
    double Linear(std::uint8_t channel)
    {
        double c = channel / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

    double Luminance(FrameColor c)
    {
        return 0.2126 * Linear(c.R) + 0.7152 * Linear(c.G) + 0.0722 * Linear(c.B);
    }

    /// WCAG's contrast ratio, where 3:1 is the least for graphics.
    double Contrast(FrameColor a, FrameColor b)
    {
        auto [low, high] = std::minmax(Luminance(a), Luminance(b));
        return (high + 0.05) / (low + 0.05);
    }
}

TEST(Below_idle_uses_the_system_border)
{
    CHECK(ColorFor(0).UseSystemDefault);
    CHECK(ColorFor(0.019).UseSystemDefault);
    CHECK(!ColorFor(0.02).UseSystemDefault);
}

TEST(Each_stop_gives_its_colour)
{
    CHECK((ColorFor(0.02) == FrameColor{ false, 0x2F, 0x80, 0xDE }));
    CHECK((ColorFor(0.25) == FrameColor{ false, 0x0B, 0x92, 0x87 }));
    CHECK((ColorFor(1.0) == FrameColor{ false, 0xB2, 0x80, 0x00 }));
    CHECK((ColorFor(2.0) == FrameColor{ false, 0xE2, 0x66, 0x00 }));
    CHECK((ColorFor(4.0) == FrameColor{ false, 0xDC, 0x2F, 0x2F }));
}

TEST(Past_the_last_stop_stays_red)
{
    CHECK((ColorFor(32.0) == FrameColor{ false, 0xDC, 0x2F, 0x2F }));
}

TEST(Every_level_stands_out_on_light_and_dark_windows)
{
    // White, Windows' light and dark surfaces, Mica's dark, and black.
    FrameColor const windows[] = {
        { false, 0xFF, 0xFF, 0xFF }, { false, 0xF3, 0xF3, 0xF3 }, { false, 0x20, 0x20, 0x20 },
        { false, 0x2B, 0x2B, 0x2B }, { false, 0x00, 0x00, 0x00 },
    };

    for (double rate = 0.02; rate < 8.0; rate *= 1.01)
        for (auto const& window : windows)
            CHECK(Contrast(ColorFor(rate), window) >= 3.0);
}

TEST(Between_stops_the_colour_blends)
{
    auto middle = ColorFor(1.414);

    CHECK(middle != ColorFor(1.0));
    CHECK(middle != ColorFor(2.0));
    CHECK_RANGE(0x66, 0x80, middle.G);
}

TEST(A_small_change_stays_on_one_level)
{
    CHECK(ColorFor(1.40) == ColorFor(1.41));
}

TEST(A_rising_load_only_ever_moves_up_the_scale)
{
    // Each step is a new colour or the one before: nothing flickers back.
    int changes = 0;
    auto previous = ColorFor(0.02);
    for (double rate = 0.02; rate < 4.0; rate *= 1.01)
    {
        auto color = ColorFor(rate);
        if (color != previous) ++changes;
        previous = color;
    }

    CHECK_EQ(24, changes);
}

TEST(Each_word_starts_at_its_stop)
{
    CHECK(ActivityFor(0) == Activity::Resting);
    CHECK(ActivityFor(0.0199) == Activity::Resting);
    CHECK(ActivityFor(0.02) == Activity::Light);
    CHECK(ActivityFor(0.2499) == Activity::Light);
    CHECK(ActivityFor(0.25) == Activity::Working);
    CHECK(ActivityFor(0.9999) == Activity::Working);
    CHECK(ActivityFor(1.0) == Activity::Busy);
    CHECK(ActivityFor(1.9999) == Activity::Busy);
    CHECK(ActivityFor(2.0) == Activity::VeryBusy);
    CHECK(ActivityFor(3.9999) == Activity::VeryBusy);
    CHECK(ActivityFor(4.0) == Activity::FlatOut);
    CHECK(ActivityFor(64.0) == Activity::FlatOut);
}
