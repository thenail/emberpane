#include "Test.h"
#include "LayoutPresets.h"

using namespace EmberPane::Core;

namespace
{
    constexpr Rect Area{ 0, 0, 1912, 1012 };
    constexpr int Gap = 12;

    MinimumSizes Minimums() { return { { 100, 100 }, {} }; }

    size_t CountOf(LayoutPreset preset) { return MakeLayout(preset).Count(); }

    Rect BoundsOf(LayoutTree const& tree, WindowId id)
    {
        for (auto const& tile : tree.Arrange(Area, Gap, Minimums()))
            if (tile.Id == id) return tile.Bounds;
        Test::Fail(__FILE__, __LINE__, "no such area");
    }
}

TEST(Each_preset_has_the_areas_it_promises)
{
    CHECK_EQ(size_t{ 1 }, CountOf(LayoutPreset::OneAtATime));
    CHECK_EQ(size_t{ 2 }, CountOf(LayoutPreset::TwoColumns));
    CHECK_EQ(size_t{ 3 }, CountOf(LayoutPreset::ThreeColumns));
    CHECK_EQ(size_t{ 2 }, CountOf(LayoutPreset::LargeAndSmall));
    CHECK_EQ(size_t{ 3 }, CountOf(LayoutPreset::LargeAndTwoSmall));
    CHECK_EQ(size_t{ 4 }, CountOf(LayoutPreset::FourAreas));
    CHECK_EQ(size_t{ 3 }, CountOf(LayoutPreset::LargeInTheMiddle));
}

TEST(No_preset_is_left_out_of_the_panel)
{
    CHECK_EQ(size_t{ 7 }, AllPresets().size());
}

TEST(Every_preset_fills_the_area_with_exact_gaps)
{
    for (auto preset : AllPresets())
    {
        auto tiles = MakeLayout(preset).Arrange(Area, Gap, Minimums());

        for (size_t i = 0; i < tiles.size(); ++i)
        {
            auto const& a = tiles[i].Bounds;
            CHECK(a.Left >= Area.Left && a.Top >= Area.Top && a.Right <= Area.Right && a.Bottom <= Area.Bottom);

            for (size_t j = i + 1; j < tiles.size(); ++j)
            {
                auto const& b = tiles[j].Bounds;
                CHECK(a.Right <= b.Left || b.Right <= a.Left || a.Bottom <= b.Top || b.Bottom <= a.Top);
            }
        }
    }
}

TEST(One_at_a_time_is_the_whole_area)
{
    CHECK(BoundsOf(MakeLayout(LayoutPreset::OneAtATime), 1) == Area);
}

TEST(Three_columns_are_equal)
{
    auto tree = MakeLayout(LayoutPreset::ThreeColumns);

    CHECK_RANGE(625, 636, BoundsOf(tree, 1).Width());
    CHECK_RANGE(625, 636, BoundsOf(tree, 2).Width());
    CHECK_RANGE(625, 636, BoundsOf(tree, 3).Width());
}

TEST(The_large_area_is_on_the_left_and_larger)
{
    auto tree = MakeLayout(LayoutPreset::LargeAndTwoSmall);

    CHECK(BoundsOf(tree, 1).Right < BoundsOf(tree, 2).Left);
    CHECK(BoundsOf(tree, 1).Width() > BoundsOf(tree, 2).Width());
    CHECK_EQ(BoundsOf(tree, 2).Left, BoundsOf(tree, 3).Left);
    CHECK(BoundsOf(tree, 2).Bottom < BoundsOf(tree, 3).Top);
}

TEST(Four_areas_are_a_grid)
{
    auto tree = MakeLayout(LayoutPreset::FourAreas);

    CHECK_EQ(BoundsOf(tree, 1).Left, BoundsOf(tree, 3).Left);
    CHECK_EQ(BoundsOf(tree, 2).Left, BoundsOf(tree, 4).Left);
    CHECK_EQ(BoundsOf(tree, 1).Top, BoundsOf(tree, 2).Top);
    CHECK_EQ(BoundsOf(tree, 3).Top, BoundsOf(tree, 4).Top);
}

TEST(Large_in_the_middle_is_a_half_between_quarters)
{
    auto tree = MakeLayout(LayoutPreset::LargeInTheMiddle);

    // Area 1 is the middle, so the window in use is dealt to it.
    CHECK(BoundsOf(tree, 2).Right < BoundsOf(tree, 1).Left);
    CHECK(BoundsOf(tree, 1).Right < BoundsOf(tree, 3).Left);
    CHECK_RANGE(940, 950, BoundsOf(tree, 1).Width());
    CHECK_RANGE(465, 480, BoundsOf(tree, 2).Width());
    CHECK_RANGE(465, 480, BoundsOf(tree, 3).Width());
}

TEST(A_dragged_edge_still_resizes_an_area)
{
    auto tree = MakeLayout(LayoutPreset::TwoColumns);

    CHECK(tree.MoveEdge(1, Edge::Right, 700, Area, Gap, Minimums()));

    CHECK_EQ(700, BoundsOf(tree, 1).Right);
    CHECK_EQ(700 + Gap, BoundsOf(tree, 2).Left);
}

TEST(The_pictures_match_the_areas)
{
    CHECK_EQ(size_t{ 4 }, PictureOf(LayoutPreset::FourAreas).size());
    CHECK((PictureOf(LayoutPreset::OneAtATime)[0] == Rect{ 0, 0, 1000, 1000 }));
}

TEST(A_resized_layout_draws_resized)
{
    auto tree = MakeLayout(LayoutPreset::TwoColumns);
    CHECK(tree.MoveEdge(1, Edge::Right, 1400, Area, Gap, Minimums()));

    auto picture = PictureOf(tree);
    CHECK_EQ(size_t{ 2 }, picture.size());
    CHECK(picture[0].Width() > picture[1].Width());
    CHECK_EQ(picture[0].Right, picture[1].Left);
}

TEST(Only_a_moved_edge_counts_as_resized)
{
    for (auto preset : AllPresets())
        CHECK(!IsResized(MakeLayout(preset), preset));

    auto tree = MakeLayout(LayoutPreset::LargeAndTwoSmall);
    CHECK(tree.MoveEdge(2, Edge::Bottom, 300, Area, Gap, Minimums()));
    CHECK(IsResized(tree, LayoutPreset::LargeAndTwoSmall));

    // Back where it was, give or take a pixel of the screen.
    auto twice = MakeLayout(LayoutPreset::TwoColumns);
    CHECK(twice.MoveEdge(1, Edge::Right, 1200, Area, Gap, Minimums()));
    CHECK(twice.MoveEdge(1, Edge::Right, (Area.Width() - Gap) / 2, Area, Gap, Minimums()));
    CHECK(!IsResized(twice, LayoutPreset::TwoColumns));
}

TEST(Settings_remember_the_layout)
{
    AppSettings s;
    CHECK(s.Arrange.Layout == LayoutPreset::TwoColumns);

    s.Arrange.Layout = LayoutPreset::FourAreas;
    CHECK(AppSettings::FromJson(s.ToJson()).Arrange.Layout == LayoutPreset::FourAreas);
}

TEST(Settings_remember_the_new_layout)
{
    AppSettings s;
    s.Arrange.Layout = LayoutPreset::LargeInTheMiddle;
    CHECK(AppSettings::FromJson(s.ToJson()).Arrange.Layout == LayoutPreset::LargeInTheMiddle);
}

// ------------------------------------------------------------ what Snap chose

namespace
{
    /// A work area of 1920 by 1032, as on a 1080p monitor with the taskbar,
    /// standing to the right of another.
    constexpr Rect Work{ 1920, 0, 3840, 1032 };

    /// A place on Work, in its quarters, twelfths and so on, as Snap would
    /// make it: Left, Top, Right, Bottom as shares of the work area.
    Rect Snapped(double left, double top, double right, double bottom)
    {
        auto x = [](double share) { return Work.Left + static_cast<int>(std::lround(share * Work.Width())); };
        auto y = [](double share) { return Work.Top + static_cast<int>(std::lround(share * Work.Height())); };
        return { x(left), y(top), x(right), y(bottom) };
    }

    std::optional<SnapTarget> Match(Rect const& snapped, LayoutPreset current)
    {
        return MatchSnap(snapped, Work, current, MakeLayout(current));
    }

    bool Is(std::optional<SnapTarget> const& target, LayoutPreset preset, WindowId area)
    {
        return target && target->Preset == preset && target->Area == area;
    }
}

TEST(A_snap_to_an_area_of_the_layout_in_use_keeps_the_layout)
{
    CHECK(Is(Match(Snapped(0, 0, 0.5, 1), LayoutPreset::TwoColumns), LayoutPreset::TwoColumns, 1));
    CHECK(Is(Match(Snapped(0.5, 0, 1, 1), LayoutPreset::TwoColumns), LayoutPreset::TwoColumns, 2));
    CHECK(Is(Match(Snapped(0.5, 0.5, 1, 1), LayoutPreset::FourAreas), LayoutPreset::FourAreas, 4));
    CHECK(Is(Match(Snapped(0.25, 0, 0.75, 1), LayoutPreset::LargeInTheMiddle), LayoutPreset::LargeInTheMiddle, 1));
}

TEST(Snaps_two_thirds_are_close_enough_to_the_large_area)
{
    CHECK(Is(Match(Snapped(0, 0, 2.0 / 3, 1), LayoutPreset::LargeAndSmall), LayoutPreset::LargeAndSmall, 1));
    CHECK(Is(Match(Snapped(2.0 / 3, 0, 1, 1), LayoutPreset::LargeAndSmall), LayoutPreset::LargeAndSmall, 2));
}

TEST(A_place_the_layout_in_use_also_has_in_snaps_version_keeps_it)
{
    // Snap's large and two small is split in half; the preset's is not.
    CHECK(Is(Match(Snapped(0, 0, 0.5, 1), LayoutPreset::LargeAndTwoSmall), LayoutPreset::LargeAndTwoSmall, 1));
    CHECK(Is(Match(Snapped(0.5, 0, 1, 0.5), LayoutPreset::LargeAndTwoSmall), LayoutPreset::LargeAndTwoSmall, 2));
    CHECK(Is(Match(Snapped(0.5, 0.5, 1, 1), LayoutPreset::LargeAndTwoSmall), LayoutPreset::LargeAndTwoSmall, 3));
}

TEST(A_place_only_another_layout_has_changes_the_layout)
{
    CHECK(Is(Match(Snapped(0, 0, 0.5, 0.5), LayoutPreset::TwoColumns), LayoutPreset::FourAreas, 1));
    CHECK(Is(Match(Snapped(0, 0, 2.0 / 3, 1), LayoutPreset::TwoColumns), LayoutPreset::LargeAndSmall, 1));
    CHECK(Is(Match(Snapped(0, 0, 1.0 / 3, 1), LayoutPreset::TwoColumns), LayoutPreset::ThreeColumns, 1));
    CHECK(Is(Match(Snapped(1.0 / 3, 0, 2.0 / 3, 1), LayoutPreset::TwoColumns), LayoutPreset::ThreeColumns, 2));
    CHECK(Is(Match(Snapped(0.25, 0, 0.75, 1), LayoutPreset::TwoColumns), LayoutPreset::LargeInTheMiddle, 1));
    CHECK(Is(Match(Snapped(0, 0, 0.25, 1), LayoutPreset::TwoColumns), LayoutPreset::LargeInTheMiddle, 2));
    CHECK(Is(Match(Snapped(0.75, 0, 1, 1), LayoutPreset::TwoColumns), LayoutPreset::LargeInTheMiddle, 3));
    CHECK(Is(Match(Snapped(0, 0, 0.5, 1), LayoutPreset::OneAtATime), LayoutPreset::TwoColumns, 1));
}

TEST(Of_the_layouts_with_that_place_the_simplest_is_chosen)
{
    // The right third is in two columns' neighbours: large and small, with
    // two areas, and three columns, with three.
    CHECK(Is(Match(Snapped(2.0 / 3, 0, 1, 1), LayoutPreset::TwoColumns), LayoutPreset::LargeAndSmall, 2));

    // The top right quarter: large and two small, then four areas.
    CHECK(Is(Match(Snapped(0.5, 0, 1, 0.5), LayoutPreset::ThreeColumns), LayoutPreset::LargeAndTwoSmall, 2));
}

TEST(A_resized_layout_is_matched_as_it_stands)
{
    auto tree = MakeLayout(LayoutPreset::TwoColumns);
    CHECK(tree.MoveEdge(1, Edge::Right, 1400, Area, Gap, Minimums()));

    // About 0.73 of the width now: that is area 1, not Snap's two thirds.
    auto target = MatchSnap(Snapped(0, 0, 0.73, 1), Work, LayoutPreset::TwoColumns, tree);
    CHECK(Is(target, LayoutPreset::TwoColumns, 1));
}

TEST(A_place_in_no_layout_goes_to_the_area_holding_most_of_it)
{
    // A monitor stood on end, where Snap stacks its halves: no preset has
    // them, and the top half is in both columns equally.
    constexpr Rect Tall{ 0, 0, 1080, 1872 };
    auto top = MatchSnap({ 0, 0, 1080, 936 }, Tall, LayoutPreset::TwoColumns, MakeLayout(LayoutPreset::TwoColumns));
    CHECK(!top);

    // Most of it in the right column.
    auto right = MatchSnap({ 400, 0, 1080, 936 }, Tall, LayoutPreset::TwoColumns, MakeLayout(LayoutPreset::TwoColumns));
    CHECK(Is(right, LayoutPreset::TwoColumns, 2));
}

TEST(Nothing_is_matched_on_an_empty_work_area)
{
    CHECK(!MatchSnap(Snapped(0, 0, 0.5, 1), Rect{}, LayoutPreset::TwoColumns, MakeLayout(LayoutPreset::TwoColumns)));
}

// ------------------------------------------------------------ several at once

namespace
{
    std::optional<SnapGroup> MatchAll(std::vector<Rect> const& snapped, LayoutPreset current)
    {
        return MatchSnaps(snapped, Work, current, MakeLayout(current));
    }

    bool Are(std::optional<SnapGroup> const& group, LayoutPreset preset, std::vector<std::optional<WindowId>> areas)
    {
        return group && group->Preset == preset && group->Areas == areas;
    }
}

TEST(A_suggested_half_and_two_quarters_is_large_and_two_small)
{
    // From four areas: the half is in no quarter, and only large and two
    // small has all three places. Taken one by one, the quarters came first
    // and kept four areas.
    auto group = MatchAll({ Snapped(0, 0, 0.5, 1), Snapped(0.5, 0, 1, 0.5), Snapped(0.5, 0.5, 1, 1) },
                          LayoutPreset::FourAreas);
    CHECK(Are(group, LayoutPreset::LargeAndTwoSmall, { 1, 2, 3 }));
}

TEST(Snaps_the_layout_in_use_has_keep_it)
{
    auto group = MatchAll({ Snapped(0.5, 0, 1, 0.5), Snapped(0, 0.5, 0.5, 1) }, LayoutPreset::FourAreas);
    CHECK(Are(group, LayoutPreset::FourAreas, { 2, 3 }));
}

TEST(Of_the_presets_with_every_place_the_simplest_is_chosen)
{
    // Two halves: two columns, though large and two small has the left one.
    auto group = MatchAll({ Snapped(0, 0, 0.5, 1), Snapped(0.5, 0, 1, 1) }, LayoutPreset::ThreeColumns);
    CHECK(Are(group, LayoutPreset::TwoColumns, { 1, 2 }));
}

TEST(With_no_preset_for_them_all_the_chosen_window_decides)
{
    // A third and a quarter are in no preset together: the third decides,
    // and the quarter has no area of three columns.
    auto group = MatchAll({ Snapped(0, 0, 1.0 / 3, 1), Snapped(0.75, 0, 1, 1) }, LayoutPreset::TwoColumns);
    CHECK(Are(group, LayoutPreset::ThreeColumns, { 1, std::nullopt }));
}

TEST(One_snap_alone_matches_as_before)
{
    auto group = MatchAll({ Snapped(0, 0, 0.5, 0.5) }, LayoutPreset::TwoColumns);
    CHECK(Are(group, LayoutPreset::FourAreas, { 1 }));

    CHECK(!MatchAll({}, LayoutPreset::TwoColumns));
}
