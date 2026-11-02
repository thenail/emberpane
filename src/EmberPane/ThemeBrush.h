#pragma once

#include <winrt/Windows.UI.ViewManagement.h>

namespace EmberPane
{
    /// One of the app's own theme brushes (App.xaml's ThemeDictionaries), for
    /// shapes built in code. XAML resolves {ThemeResource} by itself; code
    /// has to pick the dictionary for the element's theme, and look again when
    /// ActualThemeChanged says the theme moved.
    inline winrt::Microsoft::UI::Xaml::Media::Brush ThemeBrush(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& element, wchar_t const* key)
    {
        using namespace winrt::Microsoft::UI::Xaml;

        static winrt::Windows::UI::ViewManagement::AccessibilitySettings accessibility;

        wchar_t const* theme = accessibility.HighContrast() ? L"HighContrast"
                             : element.ActualTheme() == ElementTheme::Dark ? L"Dark"
                             : L"Light";

        auto dictionaries = Application::Current().Resources().ThemeDictionaries();
        auto dictionary = dictionaries.Lookup(winrt::box_value(theme)).as<ResourceDictionary>();
        return dictionary.Lookup(winrt::box_value(key)).as<Media::Brush>();
    }
}
