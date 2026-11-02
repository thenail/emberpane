#include "Test.h"
#include "FramePlan.h"

using namespace EmberPane::Core;

TEST(The_default_frame_stands_just_outside_a_dark_border)
{
    auto plan = PlanFrame(2, 2, 2, 12);

    CHECK(plan.Layered);
    CHECK(plan.DarkBorder);
    CHECK(!plan.NoBorder);
    CHECK_EQ(0, plan.DarkTo);
    CHECK_EQ(0, plan.ColorFrom);
    CHECK_EQ(2, plan.ColorTo);
}

TEST(A_see_through_line_turns_the_border_off)
{
    auto plan = PlanFrame(2, 2, 2, 12, true);

    CHECK(plan.Layered);
    CHECK(plan.NoBorder);
    CHECK(!plan.DarkBorder);
}

TEST(A_see_through_line_leaves_the_colour_where_it_is)
{
    for (int inner : { 1, 2, 3, 4 })
    {
        for (int color : { 1, 2, 3 })
        {
            auto dark = PlanFrame(color, inner, 2, 16);
            auto clear = PlanFrame(color, inner, 2, 16, true);

            CHECK_EQ(dark.ColorFrom, clear.ColorFrom);
            CHECK_EQ(dark.ColorTo, clear.ColorTo);
        }
    }
}

TEST(A_see_through_line_draws_nothing_dark)
{
    CHECK_EQ(0, PlanFrame(2, 4, 2, 16, true).DarkTo);
}

TEST(A_see_through_line_takes_the_same_room)
{
    CHECK(PlanFrame(6, 2, 2, 8).ColorTo == PlanFrame(6, 2, 2, 8, true).ColorTo);
    CHECK(PlanFrame(2, 4, 2, 2).Layered == PlanFrame(2, 4, 2, 2, true).Layered);
}

TEST(Without_a_line_a_colour_as_wide_as_Windows_border_is_Windows_border)
{
    CHECK(!PlanFrame(2, 0, 2, 12).Layered);
}

TEST(A_colour_narrower_than_Windows_border_is_Windows_border)
{
    CHECK(!PlanFrame(1, 0, 2, 12).Layered);
}

TEST(A_wider_colour_starts_at_Windows_border)
{
    auto plan = PlanFrame(3, 0, 2, 12);

    CHECK(plan.Layered);
    CHECK(!plan.DarkBorder);
    CHECK(!plan.NoBorder);
    CHECK_EQ(-2, plan.ColorFrom);
    CHECK_EQ(1, plan.ColorTo);
}

TEST(A_one_pixel_line_moves_the_colour_out_by_one)
{
    auto plan = PlanFrame(2, 1, 2, 12);

    CHECK(plan.DarkBorder);
    CHECK_EQ(-1, plan.ColorFrom);
    CHECK_EQ(1, plan.ColorTo);
}

TEST(A_line_wider_than_Windows_border_is_drawn_past_it)
{
    auto plan = PlanFrame(2, 3, 2, 12);

    CHECK(plan.DarkBorder);
    CHECK_EQ(-2, plan.DarkFrom);
    CHECK_EQ(1, plan.DarkTo);
    CHECK_EQ(1, plan.ColorFrom);
    CHECK_EQ(3, plan.ColorTo);
}

TEST(No_part_of_a_dark_border_is_left_outside_the_colour)
{
    auto plan = PlanFrame(1, 1, 4, 12);

    CHECK_EQ(-3, plan.ColorFrom);
    CHECK_EQ(0, plan.ColorTo);
}

TEST(The_colour_narrows_to_stay_in_its_half_of_the_gap)
{
    auto plan = PlanFrame(6, 1, 2, 8);

    CHECK_EQ(-1, plan.ColorFrom);
    CHECK_EQ(4, plan.ColorTo);
}

TEST(Without_a_gap_a_line_still_fits_inside_Windows_border)
{
    auto plan = PlanFrame(2, 1, 2, 0);

    CHECK(plan.Layered);
    CHECK_EQ(-1, plan.ColorFrom);
    CHECK_EQ(0, plan.ColorTo);
}

TEST(With_no_room_at_all_Windows_border_is_the_frame)
{
    auto plan = PlanFrame(2, 4, 2, 2);

    CHECK(!plan.Layered);
    CHECK(!plan.DarkBorder);
}

TEST(A_see_through_line_of_nothing_is_no_line)
{
    CHECK(PlanFrame(2, 0, 2, 12, true) == PlanFrame(2, 0, 2, 12));
    CHECK(PlanFrame(3, 0, 2, 12, true) == PlanFrame(3, 0, 2, 12));
}
