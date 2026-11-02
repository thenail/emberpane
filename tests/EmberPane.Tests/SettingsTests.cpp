#include "Test.h"
#include "Json.h"
#include "Settings.h"

#include <windows.h>

using namespace EmberPane::Core;

namespace
{
    std::wstring TempFile(wchar_t const* name)
    {
        wchar_t directory[MAX_PATH]{};
        GetTempPathW(MAX_PATH, directory);
        return std::wstring(directory) + name;
    }

    AppSettings Parse(std::wstring_view text)
    {
        auto root = Json::Parse(text);
        CHECK(root.has_value());
        return AppSettings::FromJson(*root);
    }
}

TEST(Arranging_starts_off_and_frames_start_on)
{
    AppSettings s;

    CHECK(!s.Arrange.Enabled);
    CHECK(s.Frames.Enabled);
    CHECK_EQ(8, s.Arrange.GapDip);
    CHECK_EQ(10, s.Frames.WindowSeconds);
}

TEST(Settings_survive_a_round_trip)
{
    AppSettings s;
    s.Arrange.Enabled = true;
    s.Arrange.GapDip = 4;
    s.Arrange.MarginDip = 20;
    s.Arrange.Corners = CornerStyle::RoundSmall;
    s.Arrange.GapLine = false;
    s.Frames.Enabled = false;
    s.Frames.WindowSeconds = 30;
    s.Rules.push_back({ L"calculatorapp.exe", L"", L"", Disposition::Float });
    s.Rules.push_back({ L"app.exe", L"SomeClass", L"Picture", Disposition::Ignore });

    auto back = AppSettings::FromJson(s.ToJson());

    CHECK(back.Arrange.Enabled);
    CHECK_EQ(4, back.Arrange.GapDip);
    CHECK_EQ(20, back.Arrange.MarginDip);
    CHECK(back.Arrange.Corners == CornerStyle::RoundSmall);
    CHECK(!back.Arrange.GapLine);
    CHECK(!back.Frames.Enabled);
    CHECK_EQ(30, back.Frames.WindowSeconds);
    CHECK(back.Rules == s.Rules);
}

TEST(Settings_survive_a_save_and_load)
{
    auto path = TempFile(L"EmberPane.Tests.settings.json");

    AppSettings s;
    s.Arrange.GapDip = 3;
    CHECK(s.Save(path));

    CHECK_EQ(3, AppSettings::Load(path).Arrange.GapDip);
    DeleteFileW(path.c_str());
}

TEST(A_missing_file_gives_defaults)
{
    auto s = AppSettings::Load(TempFile(L"EmberPane.Tests.no-such-file.json"));

    CHECK_EQ(8, s.Arrange.GapDip);
}

TEST(A_corrupt_file_gives_defaults)
{
    auto path = TempFile(L"EmberPane.Tests.corrupt.json");

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    WriteFile(file, "{ not json", 10, &written, nullptr);
    CloseHandle(file);

    auto s = AppSettings::Load(path);

    CHECK(!s.Arrange.Enabled);
    CHECK_EQ(8, s.Arrange.GapDip);
    DeleteFileW(path.c_str());
}

TEST(Unknown_fields_are_ignored)
{
    auto s = Parse(LR"({ "Arrange": { "GapDip": 5, "FromTheFuture": 1 }, "AlsoNew": [] })");

    CHECK_EQ(5, s.Arrange.GapDip);
}

TEST(A_gap_past_the_limit_is_brought_back_to_it)
{
    // Past the limit, part of the gap between windows no longer resizes them.
    CHECK_EQ(AppSettings::MaxGapDip, Parse(LR"({ "Arrange": { "GapDip": 32 } })").Arrange.GapDip);
    CHECK_EQ(0, Parse(LR"({ "Arrange": { "GapDip": -4 } })").Arrange.GapDip);
    CHECK_EQ(AppSettings::MaxMarginDip, Parse(LR"({ "Arrange": { "MarginDip": 500 } })").Arrange.MarginDip);
}

TEST(A_measuring_window_we_do_not_offer_keeps_the_default)
{
    CHECK_EQ(10, Parse(LR"({ "Frames": { "WindowSeconds": 7 } })").Frames.WindowSeconds);
}

TEST(A_rule_without_an_app_or_a_known_mode_is_dropped)
{
    auto s = Parse(LR"({ "Rules": [
        { "Mode": "Float" },
        { "Exe": "a.exe", "Mode": "Sideways" },
        { "Exe": "b.exe", "Mode": "Tile" }
    ] })");

    CHECK_EQ(size_t{ 1 }, s.Rules.size());
    CHECK_EQ(std::wstring(L"b.exe"), s.Rules[0].Exe);
    CHECK(s.Rules[0].Mode == Disposition::Tile);
}
