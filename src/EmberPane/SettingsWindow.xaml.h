#pragma once

#include "SettingsWindow.g.h"

namespace winrt::EmberPane::implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow>
    {
        SettingsWindow();

        void RestoreAndActivate();

    private:
        void Load();
        void ReadForm();
        void BuildColourKey();
        void BuildRules();
        void OpenAddRule();
        fire_and_forget ShowRunAtLoginAsync();
        fire_and_forget SetRunAtLoginAsync(bool wanted);
        void SetToggle(Microsoft::UI::Xaml::Controls::ToggleSwitch const& toggle,
                       Microsoft::UI::Xaml::Controls::TextBlock const& state, bool on);

        bool m_loading{ true };
        HICON m_icon{};
        int m_settingsToken{};
        int m_windowsToken{};
    };
}

namespace winrt::EmberPane::factory_implementation
{
    struct SettingsWindow : SettingsWindowT<SettingsWindow, implementation::SettingsWindow>
    {
    };
}
