#include "Test.h"
#include "LayoutPresets.h"
#include "LayoutState.h"

using namespace EmberPane::Core;

namespace
{
    constexpr Rect Area{ 0, 0, 1912, 1012 };
    constexpr int Gap = 12;

    MinimumSizes Minimums() { return { { 100, 100 }, {} }; }
}

TEST(A_resized_tree_comes_back_the_same_size)
{
    auto resized = MakeLayout(LayoutPreset::LargeAndTwoSmall);
    CHECK(resized.MoveEdge(1, Edge::Right, 700, Area, Gap, Minimums()));
    CHECK(resized.MoveEdge(2, Edge::Bottom, 300, Area, Gap, Minimums()));

    auto restored = MakeLayout(LayoutPreset::LargeAndTwoSmall);
    CHECK(restored.SetRatios(resized.Ratios()));

    auto before = resized.Arrange(Area, Gap, Minimums());
    auto after = restored.Arrange(Area, Gap, Minimums());
    CHECK_EQ(before.size(), after.size());
    for (size_t i = 0; i < before.size(); ++i)
        CHECK(before[i].Bounds == after[i].Bounds);
}

TEST(Ratios_that_do_not_fit_the_tree_change_nothing)
{
    auto tree = MakeLayout(LayoutPreset::ThreeColumns);
    auto was = tree.Ratios();

    CHECK(!tree.SetRatios({ 0.5 }));
    CHECK(!tree.SetRatios({ 0.5, 1.5 }));
    CHECK(!tree.SetRatios({ 0.5, 0.0 }));
    CHECK(tree.Ratios() == was);
}

TEST(The_layout_state_survives_a_round_trip)
{
    LayoutState state;
    state.Preset = LayoutPreset::FourAreas;
    state.Boot = 13'412'345'678'901;
    state.Layouts.push_back({ L"\\\\?\\DISPLAY#GSM5B09#1", L"{8F1E3A2B-0000-4C5D-9E6F-123456789ABC}",
                              { 0.62, 0.4, 0.5 }, { { 0x1A2B3C, 4242, 1, { 0, 140, 0, 0 } }, { 0x2B3C4D, 4243, 3 } } });
    state.Layouts.push_back({ L"\\\\?\\DISPLAY#DEL4123#2", L"", { 0.5, 0.5, 0.5 }, {} });
    state.Returning.push_back({ 0x3C4D5E, 4244, 2 });
    state.Apps.push_back({ L"kabelpost.exe", LayoutPreset::LargeAndSmall, 1 });
    state.Apps.push_back({ L"code.exe", LayoutPreset::FourAreas, 3 });

    auto text = Json::Write(state.ToJson());
    auto parsed = Json::Parse(text);
    CHECK(parsed.has_value());

    auto back = LayoutState::FromJson(*parsed);
    CHECK(back.has_value());
    CHECK(*back == state);
}

TEST(A_state_from_another_version_is_not_read)
{
    auto json = LayoutState{}.ToJson();
    json.Set(L"Version", 99);
    CHECK(!LayoutState::FromJson(json).has_value());
}

TEST(A_state_written_before_insets_reads_as_none)
{
    auto parsed = Json::Parse(LR"({"Version":1,"Preset":3,"Boot":1,"Layouts":[{"Monitor":"M","Desktop":"",)"
                              LR"("Ratios":[0.62],"Members":[{"Window":7,"Process":8,"Area":2}]}],"Returning":[]})");
    CHECK(parsed.has_value());

    auto state = LayoutState::FromJson(*parsed);
    CHECK(state.has_value());
    CHECK_EQ(state->Layouts.size(), size_t{ 1 });
    CHECK(state->Layouts[0].Members[0].Insets.Empty());
}
