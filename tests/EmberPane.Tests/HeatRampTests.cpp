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
        double first = Luminance(a), second = Luminance(b);
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
    }
}

TEST(Below_idle_uses_the_system_border)
{
    CHECK(ColorFor(0).UseSystemDefault);
    CHECK(ColorFor(0.019).UseSystemDefault);
    CHECK(!ColorFor(0.02).UseSystemDefault);
}

TEST(A_lit_frame_stays_lit_just_below_idle)
{
    auto first = ColorFor(0.02);
    CHECK((ColorFor(0.015, ColorTheme::Bright, true) == first));
    CHECK((ColorFor(IdleRelease, ColorTheme::Bright, true) == first));
    CHECK(ColorFor(0.009, ColorTheme::Bright, true).UseSystemDefault);

    // Not lit yet: the first stop is still where colour starts.
    CHECK(ColorFor(0.015, ColorTheme::Bright, false).UseSystemDefault);

    // Above idle, being lit changes nothing.
    CHECK((ColorFor(0.5, ColorTheme::Bright, true) == ColorFor(0.5)));
}

TEST(Each_stop_gives_its_colour)
{
    CHECK((ColorFor(0.02) == FrameColor{ false, 0x4C, 0x9B, 0xE8 }));
    CHECK((ColorFor(0.25) == FrameColor{ false, 0x2E, 0xC4, 0xB6 }));
    CHECK((ColorFor(1.0) == FrameColor{ false, 0xF2, 0xC1, 0x2E }));
    CHECK((ColorFor(2.0) == FrameColor{ false, 0xF2, 0x71, 0x1C }));
    CHECK((ColorFor(4.0) == FrameColor{ false, 0xE0, 0x31, 0x31 }));
}

TEST(Past_the_last_stop_stays_red)
{
    CHECK((ColorFor(32.0) == FrameColor{ false, 0xE0, 0x31, 0x31 }));
}

TEST(Each_theme_has_its_own_colours)
{
    for (double rate : { 0.02, 0.25, 1.0, 2.0, 4.0 })
    {
        CHECK(ColorFor(rate, ColorTheme::Bright) != ColorFor(rate, ColorTheme::Balanced));
        CHECK(ColorFor(rate, ColorTheme::Bright) != ColorFor(rate, ColorTheme::Ember));
        CHECK(ColorFor(rate, ColorTheme::Bright) != ColorFor(rate, ColorTheme::Aurora));
        CHECK(ColorFor(rate, ColorTheme::Ember) != ColorFor(rate, ColorTheme::Aurora));
    }
}

TEST(Below_idle_is_the_system_border_in_every_theme)
{
    for (auto theme : { ColorTheme::Bright, ColorTheme::Balanced, ColorTheme::Ember, ColorTheme::Aurora })
        CHECK(ColorFor(0.01, theme).UseSystemDefault);
}

TEST(Balanced_stands_out_on_light_and_dark_windows)
{
    // White, Windows' light and dark surfaces, Mica's dark, and black.
    FrameColor const windows[] = {
        { false, 0xFF, 0xFF, 0xFF }, { false, 0xF3, 0xF3, 0xF3 }, { false, 0x20, 0x20, 0x20 },
        { false, 0x2B, 0x2B, 0x2B }, { false, 0x00, 0x00, 0x00 },
    };

    for (double rate = 0.02; rate < 8.0; rate *= 1.01)
        for (auto const& window : windows)
            CHECK(Contrast(ColorFor(rate, ColorTheme::Balanced), window) >= 3.0);
}

TEST(Every_theme_stands_out_on_the_dark_line)
{
    for (auto theme : { ColorTheme::Bright, ColorTheme::Balanced, ColorTheme::Ember, ColorTheme::Aurora })
        for (double rate = 0.02; rate < 8.0; rate *= 1.01)
            CHECK(Contrast(ColorFor(rate, theme), DarkLine) >= 3.0);
}

TEST(Between_stops_the_colour_blends)
{
    auto middle = ColorFor(1.414);

    CHECK(middle != ColorFor(1.0));
    CHECK(middle != ColorFor(2.0));
    CHECK_RANGE(0x71, 0xC1, middle.G);
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
