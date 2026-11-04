#pragma once

#include <memory>
#include "Event.h"
#include "Geometry.h"
#include "Managed/WindowMirror.h"
#include "Settings.h"

namespace EmberPane
{
    /// What the windows share in the UI process: copies of the host's settings
    /// and window list, kept current over the link (see Link.h). Owned by the
    /// App, reached by the windows through Services::Get().
    struct Services
    {
        Core::AppSettings Settings;
        Managed::WindowMirror Windows;

        /// Settings changed - here or in the host.
        Event<> SettingsChanged;

        /// Where things stand, for the panel to show; empty means all is well.
        Event<std::wstring const&> StatusChanged;

        /// The host's layout in use, and its areas on the picture's square
        /// when they have been resized away from the preset's; empty when not.
        Core::LayoutPreset AreasPreset{ Core::LayoutPreset::TwoColumns };
        std::vector<Core::Rect> ResizedAreas;
        Event<> AreasChanged;

        /// Puts the resized areas back as the preset has them.
        std::function<void()> ResetAreas;

        /// Opening Settings is asked for from the flyout's gear and from the
        /// tray icon; the App owns the window.
        std::function<void()> OpenSettings;

        /// Quit, from the settings window: the host and the UI both.
        std::function<void()> Quit;

        /// Hands the settings to the host, which owns the file and applies
        /// them.
        std::function<void()> SaveSettings;

        /// Persists the settings and tells everyone who cares.
        void SaveAndNotify()
        {
            if (SaveSettings) SaveSettings();
            SettingsChanged();
        }

        static Services& Get();
        static void Set(Services* services);
    };
}
