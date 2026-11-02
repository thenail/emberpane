#include "pch.h"
#include "SettingsWindow.xaml.h"
#include "AppIcon.h"
#if __has_include("SettingsWindow.g.cpp")
#include "SettingsWindow.g.cpp"
#endif
#include "Controls/OffsetStepper.h"
#include "Controls/SettingsCard.h"
#include "HeatRamp.h"
#include "Native/Shell.h"
#include "Services.h"
#include "Strings.h"

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using ::EmberPane::Services;
namespace Strings = ::EmberPane::Strings;
namespace Core = ::EmberPane::Core;

namespace winrt::EmberPane::implementation
{
    namespace
    {
        constexpr int ReactSeconds[] = { 5, 10, 30 };

        int IndexOfMode(Core::Disposition mode)
        {
            switch (mode)
            {
            case Core::Disposition::Tile: return 0;
            case Core::Disposition::Float: return 1;
            default: return 2;
            }
        }

        Core::Disposition ModeAt(int index)
        {
            switch (index)
            {
            case 0: return Core::Disposition::Tile;
            case 1: return Core::Disposition::Float;
            default: return Core::Disposition::Ignore;
            }
        }

        hstring NumberText(int value) { return to_hstring(value); }

        /// Under the number, so that it means something without a unit.
        hstring SizeWord(int value, int maximum)
        {
            auto key = value == 0 ? L"SizeNone" : value * 3 <= maximum ? L"SizeSmall"
                     : value * 3 <= maximum * 2 ? L"SizeMedium" : L"SizeLarge";
            return hstring{ Strings::Get(key) };
        }

        /// The name a person knows, while the app is open; the exe otherwise.
        hstring DisplayName(std::wstring const& exe)
        {
            for (auto const& window : Services::Get().Windows.Windows())
                if (_wcsicmp(window.Exe.c_str(), exe.c_str()) == 0)
                    return hstring{ window.App };
            return hstring{ exe };
        }
    }

    SettingsWindow::SettingsWindow()
    {
        InitializeComponent();

        // The title lives in the title bar, as in the Settings app. The
        // caption buttons stay Windows' own, snap layouts and all.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
        AppWindow().TitleBar().PreferredHeightOption(TitleBarHeightOption::Tall);

        // The caption buttons are drawn by Windows for the SYSTEM theme; when
        // the window's own theme differs they vanish into the background, so
        // they are recoloured from the window's theme whenever it changes.
        auto paintCaption = [this]
        {
            bool dark = Root().ActualTheme() == ElementTheme::Dark;
            auto titleBar = AppWindow().TitleBar();
            Windows::UI::Color text = dark ? Windows::UI::Color{ 255, 255, 255, 255 } : Windows::UI::Color{ 228, 0, 0, 0 };
            Windows::UI::Color dim = dark ? Windows::UI::Color{ 93, 255, 255, 255 } : Windows::UI::Color{ 92, 0, 0, 0 };
            Windows::UI::Color hover = dark ? Windows::UI::Color{ 15, 255, 255, 255 } : Windows::UI::Color{ 9, 0, 0, 0 };
            Windows::UI::Color press = dark ? Windows::UI::Color{ 10, 255, 255, 255 } : Windows::UI::Color{ 6, 0, 0, 0 };
            titleBar.ButtonForegroundColor(text);
            titleBar.ButtonHoverForegroundColor(text);
            titleBar.ButtonPressedForegroundColor(dim);
            titleBar.ButtonInactiveForegroundColor(dim);
            titleBar.ButtonBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonInactiveBackgroundColor(Windows::UI::Colors::Transparent());
            titleBar.ButtonHoverBackgroundColor(hover);
            titleBar.ButtonPressedBackgroundColor(press);
        };
        Root().ActualThemeChanged([paintCaption](auto&&, auto&&) { paintCaption(); });
        Root().Loaded([paintCaption](auto&&, auto&&) { paintCaption(); });

        // Sized in DIPs for the monitor it opens on, and centred there.
        auto hwnd = GetWindowFromWindowId(AppWindow().Id());
        double scale = GetDpiForWindow(hwnd) / 96.0;
        auto area = DisplayArea::GetFromWindowId(AppWindow().Id(), DisplayAreaFallback::Primary).WorkArea();
        int width = std::min(static_cast<int>(900 * scale), area.Width);
        int height = std::min(static_cast<int>(820 * scale), area.Height);
        AppWindow().MoveAndResize({ area.X + (area.Width - width) / 2, area.Y + (area.Height - height) / 2, width, height });

        if (auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
        {
            presenter.PreferredMinimumWidth(static_cast<int>(720 * scale));
            presenter.PreferredMinimumHeight(static_cast<int>(420 * scale));
        }

        // The app's own icon, for the taskbar button and Alt+Tab. Loaded
        // large and left for the shell to scale.
        m_icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               64, 64, LR_DEFAULTCOLOR));
        AppWindow().SetIcon(GetIconIdFromIcon(m_icon));

        if (auto version = ::EmberPane::Native::Shell::Version(); !version.empty())
            VersionText().Text(Strings::Format(L"Version", { version }));

        // ---- wiring

        RunAtLogin().Toggled([this](auto&&, auto&&)
        {
            if (m_loading) return;
            SetRunAtLoginAsync(RunAtLogin().IsOn());
        });

        auto changed = [this](auto&&, auto&&) { ReadForm(); };
        ArrangeEnabled().Toggled(changed);
        GapLineEnabled().Toggled(changed);
        FramesEnabled().Toggled(changed);
        CornersBox().SelectionChanged(changed);
        ReactBox().SelectionChanged(changed);
        GapStepper().ValueChanged(changed);
        MarginStepper().ValueChanged(changed);

        AddRuleButton().Click([this](auto&&, auto&&) { OpenAddRule(); });

        QuitButton().Click([](auto&&, auto&&)
        {
            if (auto& quit = Services::Get().Quit)
                quit();
        });

        // The panel can change the same settings while this window is open.
        m_settingsToken = Services::Get().SettingsChanged.Add([this] { Load(); });

        // App names for the exceptions arrive with the window list.
        m_windowsToken = Services::Get().Windows.WindowsChanged.Add([this] { BuildRules(); });

        Closed([this](auto&&, auto&&)
        {
            Services::Get().SettingsChanged.Remove(m_settingsToken);
            Services::Get().Windows.WindowsChanged.Remove(m_windowsToken);
            if (m_icon) DestroyIcon(m_icon);
            m_icon = nullptr;
        });

        ShowRunAtLoginAsync();

        BuildColourKey();
        Load();
        m_loading = false;
    }

    void SettingsWindow::RestoreAndActivate()
    {
        // Activating alone does nothing to a minimised window - it stays in
        // the taskbar and the click looks ignored. Restore brings back a
        // window that was maximised before it was minimised as maximised.
        if (auto presenter = AppWindow().Presenter().try_as<OverlappedPresenter>())
            if (presenter.State() == OverlappedPresenterState::Minimized)
                presenter.Restore();

        Activate();
    }

    fire_and_forget SettingsWindow::ShowRunAtLoginAsync()
    {
        auto strong = get_strong();
        bool on = co_await ::EmberPane::Native::Shell::StartupEnabledAsync();
        SetToggle(RunAtLogin(), RunAtLoginState(), on);
    }

    fire_and_forget SettingsWindow::SetRunAtLoginAsync(bool wanted)
    {
        auto strong = get_strong();

        // Reflect what actually happened rather than leaving the switch
        // lying about the state.
        bool on = co_await ::EmberPane::Native::Shell::SetStartupEnabledAsync(wanted);
        SetToggle(RunAtLogin(), RunAtLoginState(), on);
    }

    void SettingsWindow::SetToggle(ToggleSwitch const& toggle, TextBlock const& state, bool on)
    {
        bool loading = m_loading;
        m_loading = true;
        toggle.IsOn(on);
        state.Text(on ? Strings::Get(L"On") : Strings::Get(L"Off"));
        m_loading = loading;
    }

    void SettingsWindow::Load()
    {
        auto const& s = Services::Get().Settings;

        bool loading = m_loading;
        m_loading = true;

        SetToggle(ArrangeEnabled(), ArrangeEnabledState(), s.Arrange.Enabled);
        SetToggle(GapLineEnabled(), GapLineState(), s.Arrange.GapLine);
        SetToggle(FramesEnabled(), FramesEnabledState(), s.Frames.Enabled);

        GapStepper().Value(s.Arrange.GapDip);
        GapStepper().ValueText(NumberText(s.Arrange.GapDip));
        GapStepper().CaptionText(SizeWord(s.Arrange.GapDip, Core::AppSettings::MaxGapDip));
        MarginStepper().Value(s.Arrange.MarginDip);
        MarginStepper().ValueText(NumberText(s.Arrange.MarginDip));
        MarginStepper().CaptionText(SizeWord(s.Arrange.MarginDip, Core::AppSettings::MaxMarginDip));

        CornersBox().SelectedIndex(static_cast<int32_t>(s.Arrange.Corners));

        auto react = std::find(std::begin(ReactSeconds), std::end(ReactSeconds), s.Frames.WindowSeconds);
        ReactBox().SelectedIndex(react != std::end(ReactSeconds) ? static_cast<int32_t>(react - std::begin(ReactSeconds)) : 1);

        BuildRules();

        m_loading = loading;
    }

    void SettingsWindow::ReadForm()
    {
        if (m_loading) return;

        auto& services = Services::Get();
        auto& s = services.Settings;

        s.Arrange.Enabled = ArrangeEnabled().IsOn();
        s.Arrange.GapLine = GapLineEnabled().IsOn();
        s.Arrange.GapDip = GapStepper().Value();
        s.Arrange.MarginDip = MarginStepper().Value();
        s.Arrange.Corners = static_cast<Core::CornerStyle>(std::max(0, CornersBox().SelectedIndex()));
        s.Frames.Enabled = FramesEnabled().IsOn();
        s.Frames.WindowSeconds = ReactSeconds[std::clamp(ReactBox().SelectedIndex(), 0, 2)];

        services.SaveAndNotify();
    }

    void SettingsWindow::BuildColourKey()
    {
        struct Entry
        {
            double Rate;
            wchar_t const* Word;
        };

        constexpr Entry Entries[] = {
            { 0.0, L"ActivityResting" }, { 0.02, L"ActivityLight" }, { 0.25, L"ActivityWorking" },
            { 1.0, L"ActivityBusy" },    { 2.0, L"ActivityVeryBusy" }, { 4.0, L"ActivityFlatOut" },
        };

        for (auto const& entry : Entries)
        {
            StackPanel item;
            item.Orientation(Orientation::Horizontal);
            item.Spacing(8);

            Shapes::Ellipse dot;
            dot.Width(10);
            dot.Height(10);
            dot.VerticalAlignment(VerticalAlignment::Center);

            auto color = Core::ColorFor(entry.Rate);
            if (color.UseSystemDefault)
            {
                dot.StrokeThickness(1);
                dot.Stroke(Application::Current().Resources().Lookup(box_value(L"TextFillColorTertiaryBrush")).as<Media::Brush>());
            }
            else
            {
                Media::SolidColorBrush fill;
                fill.Color({ 255, color.R, color.G, color.B });
                dot.Fill(fill);
            }
            item.Children().Append(dot);

            TextBlock word;
            word.Text(Strings::Get(entry.Word));
            item.Children().Append(word);

            ColourKey().Children().Append(item);
        }
    }

    void SettingsWindow::BuildRules()
    {
        RuleRows().Children().Clear();

        auto const& rules = Services::Get().Settings.Rules;
        NoRules().Visibility(rules.empty() ? Visibility::Visible : Visibility::Collapsed);

        for (size_t i = 0; i < rules.size(); ++i)
        {
            Grid row;
            row.ColumnSpacing(12);
            row.Padding(ThicknessHelper::FromLengths(16, 8, 8, 8));
            row.Background(Application::Current().Resources().Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<Media::Brush>());
            row.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));

            ColumnDefinition name;
            ColumnDefinition mode;
            mode.Width(GridLengthHelper::Auto());
            ColumnDefinition remove;
            remove.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(name);
            row.ColumnDefinitions().Append(mode);
            row.ColumnDefinitions().Append(remove);

            TextBlock app;
            app.Text(DisplayName(rules[i].Exe));
            app.VerticalAlignment(VerticalAlignment::Center);
            app.TextTrimming(TextTrimming::CharacterEllipsis);
            row.Children().Append(app);

            ComboBox box;
            box.MinWidth(220);
            for (auto key : { L"ModeArrange", L"ModeLeaveWhere", L"ModeLeaveAlone" })
                box.Items().Append(box_value(hstring{ Strings::Get(key) }));
            box.SelectedIndex(IndexOfMode(rules[i].Mode));
            Automation::AutomationProperties::SetName(box, app.Text());
            Grid::SetColumn(box, 1);
            box.SelectionChanged([this, i](Windows::Foundation::IInspectable const& sender, auto&&)
            {
                if (m_loading) return;

                auto& services = Services::Get();
                if (i >= services.Settings.Rules.size()) return;

                services.Settings.Rules[i].Mode = ModeAt(sender.as<ComboBox>().SelectedIndex());
                services.SaveAndNotify();
            });
            row.Children().Append(box);

            FontIcon bin;
            bin.Glyph(L"\uE74D");
            bin.FontSize(14);

            Button drop;
            drop.Content(bin);
            drop.Style(Application::Current().Resources().Lookup(box_value(L"SubtleButton")).as<Style>());
            auto dropName = hstring{ Strings::Get(L"RemoveRule") };
            ToolTipService::SetToolTip(drop, box_value(dropName));
            Automation::AutomationProperties::SetName(drop, dropName);
            Grid::SetColumn(drop, 2);
            drop.Click([i](auto&&, auto&&)
            {
                auto& services = Services::Get();
                if (i >= services.Settings.Rules.size()) return;

                services.Settings.Rules.erase(services.Settings.Rules.begin() + i);
                services.SaveAndNotify();
            });
            row.Children().Append(drop);

            RuleRows().Children().Append(row);
        }
    }

    void SettingsWindow::OpenAddRule()
    {
        auto const& settings = Services::Get().Settings;

        // The apps now open that have no exception yet, each once.
        std::vector<std::pair<std::wstring, std::wstring>> apps;
        for (auto const& window : Services::Get().Windows.Windows())
        {
            auto same = [&](auto const& exe) { return _wcsicmp(exe.c_str(), window.Exe.c_str()) == 0; };
            bool listed = std::any_of(apps.begin(), apps.end(), [&](auto const& app) { return same(app.first); });
            bool ruled = std::any_of(settings.Rules.begin(), settings.Rules.end(), [&](auto const& r) { return same(r.Exe); });
            if (!listed && !ruled && !window.Exe.empty())
                apps.emplace_back(window.Exe, window.App);
        }

        MenuFlyout menu;
        for (auto const& [exe, app] : apps)
        {
            MenuFlyoutItem item;
            item.Text(app);
            item.Click([exe = exe](auto&&, auto&&)
            {
                auto& services = Services::Get();
                services.Settings.Rules.push_back({ exe, L"", L"", Core::Disposition::Float });
                services.SaveAndNotify();
            });
            menu.Items().Append(item);
        }

        if (apps.empty())
        {
            MenuFlyoutItem none;
            none.Text(Strings::Get(L"NoAppsToAdd"));
            none.IsEnabled(false);
            menu.Items().Append(none);
        }

        menu.ShowAt(AddRuleButton());
    }
}
