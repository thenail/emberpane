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
}

TEST(No_preset_is_left_out_of_the_panel)
{
    CHECK_EQ(size_t{ 6 }, AllPresets().size());
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
