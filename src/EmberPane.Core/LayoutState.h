#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "Json.h"
#include "LayoutTree.h"
#include "Settings.h"

/// Which window is in which area, and how the areas were sized, kept on disk
/// so that a restart of EmberPane - an update, a crash, a quit and a launch -
/// puts every window back where it was. Windows themselves outlive EmberPane;
/// a restart of Windows does not, so the state names the boot it belongs to.
namespace EmberPane::Core
{
    /// A window, as a number: its HWND and its process, which together say it
    /// is still the same window and not a later one given the same handle.
    struct SavedWindow
    {
        std::uint64_t Window{ 0 };
        std::uint32_t Process{ 0 };
        WindowId Area{ 0 };

        /// Edges the user dragged in where there was no split to move.
        Core::Insets Insets;

        bool operator==(SavedWindow const&) const = default;
    };

    struct SavedLayout
    {
        std::wstring Monitor;
        std::wstring Desktop;

        /// LayoutTree::Ratios.
        std::vector<double> Ratios;
        std::vector<SavedWindow> Members;

        bool operator==(SavedLayout const&) const = default;
    };

    /// The area an app's windows were last dropped in, under one preset. Its
    /// windows open there. Unlike the rest, it outlives a restart of Windows:
    /// it names an app and an area, never a window.
    struct SavedApp
    {
        /// The exe's file name, in lower case.
        std::wstring App;
        LayoutPreset Preset{ LayoutPreset::TwoColumns };
        WindowId Area{ 0 };

        bool operator==(SavedApp const&) const = default;
    };

    struct LayoutState
    {
        /// The areas are this preset's; under another, none of it applies.
        LayoutPreset Preset{ LayoutPreset::TwoColumns };

        /// When Windows started, in milliseconds since 1601.
        std::int64_t Boot{ 0 };

        std::vector<SavedLayout> Layouts;

        /// Minimized windows, and the area each returns to.
        std::vector<SavedWindow> Returning;

        std::vector<SavedApp> Apps;

        bool operator==(LayoutState const&) const = default;

        /// Machine-local, beside nothing that roams: the handles mean nothing
        /// anywhere else.
        static std::wstring DefaultPath();

        Json::Value ToJson() const;

        /// Nothing when it is not a state this version wrote.
        static std::optional<LayoutState> FromJson(Json::Value const& root);
    };
}
