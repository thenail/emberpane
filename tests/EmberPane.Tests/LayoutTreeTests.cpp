#include "Test.h"
#include "LayoutTree.h"

#include <set>

using namespace EmberPane::Core;

namespace
{
    // A 16:9 work area, as at 1920 x 1080 with an 8 px margin.
    constexpr Rect Area{ 8, 8, 1912, 1032 };
    constexpr int Gap = 12;

    MinimumSizes Minimums() { return { { 100, 100 }, {} }; }

    Rect BoundsOf(LayoutTree const& tree, WindowId id, MinimumSizes const& minimums = Minimums(), Rect area = Area,
                  int gap = Gap)
    {
        for (auto const& tile : tree.Arrange(area, gap, minimums))
            if (tile.Id == id) return tile.Bounds;
        Test::Fail(__FILE__, __LINE__, "no such tile");
    }

    LayoutTree With(std::initializer_list<WindowId> windows)
    {
        LayoutTree tree;
        for (auto id : windows)
            CHECK(tree.Insert(id, std::nullopt, Area, Gap, Minimums()));
        return tree;
    }

    bool Overlap(Rect const& a, Rect const& b)
    {
        return a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;
    }

    void CheckClean(LayoutTree const& tree, Rect area, int gap, MinimumSizes const& minimums = Minimums())
    {
        auto tiles = tree.Arrange(area, gap, minimums);
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            auto const& b = tiles[i].Bounds;
            CHECK(b.Left >= area.Left && b.Top >= area.Top && b.Right <= area.Right && b.Bottom <= area.Bottom);
            for (size_t j = i + 1; j < tiles.size(); ++j)
                CHECK(!Overlap(b, tiles[j].Bounds));
        }
    }
}

TEST(A_single_window_fills_the_area)
{
    auto tree = With({ 1 });
    CHECK(BoundsOf(tree, 1) == Area);
}

TEST(Two_windows_are_separated_by_exactly_the_gap)
{
    auto tree = With({ 1, 2 });

    auto a = BoundsOf(tree, 1);
    auto b = BoundsOf(tree, 2);

    CHECK_EQ(Gap, b.Left - a.Right);
    CHECK_EQ(Area.Left, a.Left);
    CHECK_EQ(Area.Right, b.Right);
}

TEST(Windows_split_along_the_longer_side)
{
    auto wide = With({ 1, 2 });
    CHECK_EQ(BoundsOf(wide, 1).Top, BoundsOf(wide, 2).Top);

    constexpr Rect Tall{ 0, 0, 600, 1200 };
    LayoutTree tall;
    tall.Insert(1, std::nullopt, Tall, Gap, Minimums());
    tall.Insert(2, std::nullopt, Tall, Gap, Minimums());
    CHECK_EQ(BoundsOf(tall, 1, Minimums(), Tall).Left, BoundsOf(tall, 2, Minimums(), Tall).Left);
}

TEST(Four_windows_insert_into_a_grid)
{
    auto tree = With({ 1, 2, 3, 4 });

    std::set<int> lefts, tops;
    for (auto const& tile : tree.Arrange(Area, Gap, Minimums()))
    {
        lefts.insert(tile.Bounds.Left);
        tops.insert(tile.Bounds.Top);
    }

    CHECK_EQ(size_t{ 2 }, lefts.size());
    CHECK_EQ(size_t{ 2 }, tops.size());
}

TEST(The_first_window_keeps_the_largest_tile)
{
    auto tree = With({ 1, 2, 3 });

    CHECK(BoundsOf(tree, 1).Area() > BoundsOf(tree, 2).Area());
    CHECK(BoundsOf(tree, 1).Area() > BoundsOf(tree, 3).Area());
}

TEST(Removing_a_window_gives_its_space_to_its_sibling)
{
    auto tree = With({ 1, 2 });
    tree.Remove(2);

    CHECK(BoundsOf(tree, 1) == Area);
    CHECK(!tree.Contains(2));
}

TEST(Removing_keeps_the_rest_of_the_layout)
{
    auto tree = With({ 1, 2, 3 });
    auto first = BoundsOf(tree, 1);

    tree.Remove(3);

    CHECK(BoundsOf(tree, 1) == first);
    CHECK_EQ(size_t{ 2 }, tree.Count());
}

TEST(Removing_the_last_window_leaves_nothing)
{
    auto tree = With({ 1 });
    tree.Remove(1);

    CHECK(tree.Empty());
    CHECK(tree.Arrange(Area, Gap, Minimums()).empty());
}

TEST(The_gaps_are_equal_at_every_scale)
{
    for (int gap : { 0, 8, 12, 18, 24 })
    {
        LayoutTree tree;
        for (WindowId id = 1; id <= 7; ++id)
            CHECK(tree.Insert(id, std::nullopt, Area, gap, Minimums()));

        auto tiles = tree.Arrange(Area, gap, Minimums());
        for (auto const& a : tiles)
        {
            for (auto const& b : tiles)
            {
                bool besideEachOther = a.Bounds.Top < b.Bounds.Bottom && b.Bounds.Top < a.Bounds.Bottom;
                if (besideEachOther && b.Bounds.Left >= a.Bounds.Right)
                    CHECK(b.Bounds.Left - a.Bounds.Right >= gap);
            }
        }
        CheckClean(tree, Area, gap);
    }
}

TEST(Tiles_never_leave_the_area)
{
    LayoutTree tree;
    for (WindowId id = 1; id <= 12; ++id)
        tree.Insert(id, std::nullopt, Area, Gap, Minimums());

    CheckClean(tree, Area, Gap);
}

TEST(Dragging_a_right_edge_moves_the_split_beside_it)
{
    auto tree = With({ 1, 2 });

    CHECK(tree.MoveEdge(1, Edge::Right, 700, Area, Gap, Minimums()));

    CHECK_EQ(700, BoundsOf(tree, 1).Right);
    CHECK_EQ(700 + Gap, BoundsOf(tree, 2).Left);
    CHECK_EQ(Area.Right, BoundsOf(tree, 2).Right);
}

TEST(Dragging_a_left_edge_moves_the_same_split)
{
    auto tree = With({ 1, 2 });

    CHECK(tree.MoveEdge(2, Edge::Left, 900, Area, Gap, Minimums()));

    CHECK_EQ(900, BoundsOf(tree, 2).Left);
    CHECK_EQ(900 - Gap, BoundsOf(tree, 1).Right);
}

TEST(A_corner_drag_moves_two_splits)
{
    auto tree = With({ 1, 2, 3 });

    // 2 sits above 3, to the right of 1.
    CHECK(tree.MoveEdge(2, Edge::Left, 1000, Area, Gap, Minimums()));
    CHECK(tree.MoveEdge(2, Edge::Bottom, 600, Area, Gap, Minimums()));

    CHECK_EQ(1000, BoundsOf(tree, 2).Left);
    CHECK_EQ(600, BoundsOf(tree, 2).Bottom);
    CHECK_EQ(600 + Gap, BoundsOf(tree, 3).Top);
    CheckClean(tree, Area, Gap);
}

TEST(An_edge_against_the_screen_moves_nothing)
{
    auto tree = With({ 1, 2 });
    auto before = BoundsOf(tree, 1);

    CHECK(!tree.MoveEdge(1, Edge::Left, 300, Area, Gap, Minimums()));
    CHECK(!tree.MoveEdge(1, Edge::Top, 300, Area, Gap, Minimums()));

    CHECK(BoundsOf(tree, 1) == before);
}

TEST(A_minimum_size_stops_the_split)
{
    auto tree = With({ 1, 2 });

    tree.MoveEdge(1, Edge::Right, 20, Area, Gap, Minimums());
    CHECK_EQ(100, BoundsOf(tree, 1).Width());

    tree.MoveEdge(1, Edge::Right, 5000, Area, Gap, Minimums());
    CHECK_EQ(100, BoundsOf(tree, 2).Width());
}

TEST(A_window_too_large_for_its_tile_widens_it)
{
    auto tree = With({ 1, 2 });

    auto minimums = Minimums();
    minimums.Learned[2] = { 1300, 100 };

    CHECK_EQ(1300, BoundsOf(tree, 2, minimums).Width());
    CheckClean(tree, Area, Gap, minimums);
}

TEST(A_learned_minimum_never_lowers_the_default)
{
    auto minimums = Minimums();
    minimums.Learned[1] = { 40, 400 };

    CHECK((minimums.For(1) == Size{ 100, 400 }));
}

TEST(A_full_tree_refuses_a_new_window)
{
    constexpr Rect Small{ 0, 0, 300, 300 };
    MinimumSizes minimums{ { 120, 120 }, {} };

    LayoutTree tree;
    CHECK(tree.Insert(1, std::nullopt, Small, Gap, minimums));
    CHECK(tree.Insert(2, std::nullopt, Small, Gap, minimums));
    CHECK(tree.Insert(3, std::nullopt, Small, Gap, minimums));
    CHECK(tree.Insert(4, std::nullopt, Small, Gap, minimums));
    CHECK(!tree.Insert(5, std::nullopt, Small, Gap, minimums));
    CHECK_EQ(size_t{ 4 }, tree.Count());
}

TEST(An_impossible_layout_still_arranges)
{
    constexpr Rect Tiny{ 0, 0, 200, 200 };
    auto tree = With({ 1, 2, 3, 4, 5 });

    MinimumSizes minimums{ { 500, 500 }, {} };
    CHECK_EQ(size_t{ 5 }, tree.Arrange(Tiny, Gap, minimums).size());
}

TEST(Insert_splits_the_window_asked_for)
{
    auto tree = With({ 1, 2 });
    CHECK(tree.Insert(3, WindowId{ 1 }, Area, Gap, Minimums()));

    CHECK_EQ(BoundsOf(tree, 1).Left, BoundsOf(tree, 3).Left);
    CHECK(BoundsOf(tree, 2).Height() > BoundsOf(tree, 1).Height());
}

TEST(A_copy_does_not_share_anything_with_the_original)
{
    auto tree = With({ 1, 2 });
    auto copy = tree;

    copy.Remove(2);

    CHECK(tree.Contains(2));
    CHECK(!copy.Contains(2));
}

TEST(A_point_in_a_gap_finds_its_split)
{
    auto tree = With({ 1, 2 });
    int x = BoundsOf(tree, 1).Right + 3;

    auto hit = tree.GapAt({ x, 500 }, Area, Gap, Minimums());

    CHECK(hit.has_value());
    CHECK(hit->Direction == Axis::SideBySide);
    CHECK_EQ(Gap, hit->Gap.Width());
    CHECK_EQ(Area.Height(), hit->Gap.Height());
}

TEST(A_point_on_a_tile_finds_nothing)
{
    auto tree = With({ 1, 2 });
    CHECK(!tree.GapAt({ 100, 100 }, Area, Gap, Minimums()));
}

TEST(A_point_outside_the_area_finds_nothing)
{
    auto tree = With({ 1, 2 });
    CHECK(!tree.GapAt({ 2, 500 }, Area, Gap, Minimums()));
}

TEST(The_outermost_split_wins_where_gaps_cross)
{
    auto tree = With({ 1, 2, 3, 4 });

    // Where the vertical gap meets the horizontal ones.
    int x = BoundsOf(tree, 1).Right + 2;
    int y = BoundsOf(tree, 1).Bottom + 2;

    auto hit = tree.GapAt({ x, y }, Area, Gap, Minimums());

    CHECK(hit.has_value());
    CHECK(hit->Direction == Axis::SideBySide);
    CHECK_EQ(Area.Height(), hit->Gap.Height());
}

TEST(An_inner_gap_is_found_too)
{
    auto tree = With({ 1, 2, 3 });
    int y = BoundsOf(tree, 2).Bottom + 2;

    auto hit = tree.GapAt({ BoundsOf(tree, 2).Left + 50, y }, Area, Gap, Minimums());

    CHECK(hit.has_value());
    CHECK(hit->Direction == Axis::Stacked);
    CHECK_EQ(BoundsOf(tree, 2).Width(), hit->Gap.Width());
}

TEST(Moving_a_split_resizes_both_sides)
{
    auto tree = With({ 1, 2 });
    auto hit = tree.GapAt({ BoundsOf(tree, 1).Right + 1, 500 }, Area, Gap, Minimums());

    tree.MoveSplit(hit->Split, 500, Area, Gap, Minimums());

    CHECK_EQ(500, BoundsOf(tree, 1).Right);
    CHECK_EQ(500 + Gap, BoundsOf(tree, 2).Left);
}

TEST(A_split_keeps_its_identity_as_it_moves)
{
    auto tree = With({ 1, 2 });
    auto before = tree.GapAt({ BoundsOf(tree, 1).Right + 1, 500 }, Area, Gap, Minimums());

    tree.MoveSplit(before->Split, 500, Area, Gap, Minimums());
    auto after = tree.GapAt({ 502, 500 }, Area, Gap, Minimums());

    CHECK_EQ(before->Split, after->Split);
}

TEST(Near_a_gap_reaches_a_share_into_both_sides)
{
    auto tree = With({ 1, 2 });
    auto a = BoundsOf(tree, 1);
    auto b = BoundsOf(tree, 2);
    int intoA = a.Width() / 4;
    int intoB = b.Width() / 4;

    CHECK(tree.NearGap({ a.Right + 2, 500 }, Area, Gap, Minimums(), 0.25));
    CHECK(tree.NearGap({ a.Right - intoA + 2, 500 }, Area, Gap, Minimums(), 0.25));
    CHECK(tree.NearGap({ b.Left + intoB - 2, 500 }, Area, Gap, Minimums(), 0.25));

    CHECK(!tree.NearGap({ a.Right - intoA - 2, 500 }, Area, Gap, Minimums(), 0.25));
    CHECK(!tree.NearGap({ b.Left + intoB + 2, 500 }, Area, Gap, Minimums(), 0.25));
}

TEST(Near_a_gap_holds_with_no_gap_at_all)
{
    LayoutTree tree;
    CHECK(tree.Insert(1, std::nullopt, Area, 0, Minimums()));
    CHECK(tree.Insert(2, std::nullopt, Area, 0, Minimums()));
    auto a = BoundsOf(tree, 1, Minimums(), Area, 0);

    CHECK(tree.NearGap({ a.Right - 10, 500 }, Area, 0, Minimums(), 0.25));
    CHECK(tree.NearGap({ a.Right + 10, 500 }, Area, 0, Minimums(), 0.25));
}

TEST(Near_an_inner_gap_counts_too)
{
    auto tree = With({ 1, 2, 3 });
    auto upper = BoundsOf(tree, 2);
    int x = upper.Left + upper.Width() / 2;

    CHECK(tree.NearGap({ x, upper.Bottom - upper.Height() / 8 }, Area, Gap, Minimums(), 0.25));
    CHECK(!tree.NearGap({ x, upper.Top + upper.Height() / 8 }, Area, Gap, Minimums(), 0.25));
}

TEST(The_margin_and_one_area_are_never_near_a_gap)
{
    auto tree = With({ 1, 2 });
    CHECK(!tree.NearGap({ 2, 500 }, Area, Gap, Minimums(), 0.25));

    auto single = With({ 1 });
    CHECK(!single.NearGap({ Area.Left + Area.Width() / 2, 500 }, Area, Gap, Minimums(), 0.25));
}

TEST(A_point_in_a_gap_goes_to_the_nearest_tile)
{
    auto tree = With({ 1, 2 });
    auto a = BoundsOf(tree, 1);

    CHECK_EQ(WindowId{ 1 }, tree.NearestTile({ a.Right + 2, 500 }, Area, Gap, Minimums())->Id);
    CHECK_EQ(WindowId{ 2 }, tree.NearestTile({ a.Right + Gap - 2, 500 }, Area, Gap, Minimums())->Id);
}

TEST(A_point_in_the_margin_goes_to_the_nearest_tile)
{
    auto tree = With({ 1, 2 });
    CHECK_EQ(WindowId{ 1 }, tree.NearestTile({ 2, 500 }, Area, Gap, Minimums())->Id);
}

TEST(A_point_on_a_tile_is_that_tile)
{
    auto tree = With({ 1, 2 });
    CHECK_EQ(WindowId{ 2 }, tree.NearestTile({ 1500, 500 }, Area, Gap, Minimums())->Id);
}

TEST(An_empty_tree_has_no_nearest_tile)
{
    CHECK(!LayoutTree{}.NearestTile({ 5, 5 }, Area, Gap, Minimums()));
}
