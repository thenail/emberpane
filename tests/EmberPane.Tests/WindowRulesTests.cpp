#include "Test.h"
#include "WindowRules.h"

using namespace EmberPane::Core;

namespace
{
    /// An ordinary app window, as Chrome's and Notepad's read on the machine
    /// this was measured on.
    WindowFacts AppWindow()
    {
        WindowFacts f;
        f.Visible = true;
        f.HasCaption = true;
        f.Resizable = true;
        f.ClassName = L"Notepad";
        f.ExeName = L"Notepad.exe";
        f.Title = L"Untitled";
        return f;
    }

    Disposition Check(WindowFacts const& facts) { return Classify(facts, {}); }
}

TEST(An_ordinary_window_is_tiled)
{
    CHECK(Check(AppWindow()) == Disposition::Tile);
}

TEST(A_window_that_is_not_visible_is_ignored)
{
    auto f = AppWindow();
    f.Visible = false;
    CHECK(Check(f) == Disposition::Ignore);
}

TEST(Our_own_windows_are_ignored)
{
    auto f = AppWindow();
    f.OwnProcess = true;
    CHECK(Check(f) == Disposition::Ignore);
}

TEST(A_window_hiding_itself_on_this_desktop_is_ignored)
{
    auto f = AppWindow();
    f.CloakedHere = true;
    CHECK(Check(f) == Disposition::Ignore);
}

TEST(The_shell_is_ignored)
{
    for (auto name : { L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" })
    {
        auto f = AppWindow();
        f.ClassName = name;
        CHECK(Check(f) == Disposition::Ignore);
    }
}

TEST(Tool_windows_and_noactivate_windows_are_ignored)
{
    auto tool = AppWindow();
    tool.ToolWindow = true;
    CHECK(Check(tool) == Disposition::Ignore);

    auto noActivate = AppWindow();
    noActivate.NoActivate = true;
    CHECK(Check(noActivate) == Disposition::Ignore);
}

TEST(Dialogs_are_ignored)
{
    auto f = AppWindow();
    f.HasOwner = true;
    CHECK(Check(f) == Disposition::Ignore);
}

TEST(A_window_with_no_caption_is_ignored)
{
    auto f = AppWindow();
    f.HasCaption = false;
    CHECK(Check(f) == Disposition::Ignore);
}

TEST(A_window_that_cannot_be_resized_floats)
{
    auto f = AppWindow();
    f.Resizable = false;
    CHECK(Check(f) == Disposition::Float);
}

TEST(A_rule_can_tile_an_app_with_no_caption)
{
    auto f = AppWindow();
    f.HasCaption = false;
    CHECK(Classify(f, { { L"notepad.exe", L"", L"", Disposition::Tile } }) == Disposition::Tile);
}

TEST(A_rule_cannot_bring_in_a_dialog)
{
    auto f = AppWindow();
    f.HasOwner = true;
    CHECK(Classify(f, { { L"notepad.exe", L"", L"", Disposition::Tile } }) == Disposition::Ignore);
}

TEST(A_rule_matches_the_exe_whatever_its_case)
{
    CHECK(Classify(AppWindow(), { { L"NOTEPAD.EXE", L"", L"", Disposition::Float } }) == Disposition::Float);
}

TEST(A_rule_can_be_narrowed_by_class_and_title)
{
    auto f = AppWindow();
    f.Title = L"Picture in picture";

    CHECK(Matches({ L"notepad.exe", L"notepad", L"", Disposition::Float }, f));
    CHECK(Matches({ L"notepad.exe", L"", L"PICTURE", Disposition::Float }, f));
    CHECK(!Matches({ L"notepad.exe", L"OtherClass", L"", Disposition::Float }, f));
    CHECK(!Matches({ L"notepad.exe", L"", L"Settings", Disposition::Float }, f));
}

TEST(The_first_matching_rule_wins)
{
    CHECK(Classify(AppWindow(), { { L"other.exe", L"", L"", Disposition::Ignore },
                                  { L"notepad.exe", L"", L"", Disposition::Float },
                                  { L"notepad.exe", L"", L"", Disposition::Ignore } }) == Disposition::Float);
}
