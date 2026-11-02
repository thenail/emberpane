#pragma once

#include <optional>
#include <string>
#include <vector>
#include "Json.h"

namespace EmberPane::Core
{
    /// What Ember Pane does with a window. Tile places and frames it, Float
    /// frames it and leaves it where it is, Ignore leaves it alone entirely.
    enum class Disposition
    {
        Ignore,
        Float,
        Tile,
    };

    enum class CornerStyle
    {
        Round,
        RoundSmall,
        Leave,
    };

    /// The layouts offered in the panel; see LayoutPresets.h.
    enum class LayoutPreset
    {
        OneAtATime,
        TwoColumns,
        ThreeColumns,
        LargeAndSmall,
        LargeAndTwoSmall,
        FourAreas,
    };

    struct ArrangeSettings
    {
        /// Off until the user turns it on: moving every window on the desktop
        /// without being asked is a poor first impression.
        bool Enabled{ false };

        LayoutPreset Layout{ LayoutPreset::TwoColumns };

        /// In DIPs. Stops at MaxGapDip: past that, part of the gap between
        /// side-by-side windows no longer resizes them.
        int GapDip{ 8 };
        int MarginDip{ 8 };

        CornerStyle Corners{ CornerStyle::Round };

        /// The line that appears in a gap on hover.
        bool GapLine{ true };
    };

    struct FrameSettings
    {
        bool Enabled{ true };
        int WindowSeconds{ 10 };
    };

    /// A user's exception. Matches on exe name, case-insensitively, optionally
    /// narrowed by class and by a title substring.
    struct Rule
    {
        std::wstring Exe;
        std::wstring Class;
        std::wstring Title;
        Disposition Mode{ Disposition::Float };

        bool operator==(Rule const&) const = default;
    };

    /// Persisted configuration, stored as JSON under %APPDATA%\EmberPane.
    ///
    /// Loading is deliberately forgiving: a corrupt or unreadable file yields
    /// defaults rather than an error, because a tray utility that refuses to
    /// start over a bad settings file is worse than one that forgets a
    /// preference.
    struct AppSettings
    {
        static constexpr int MaxGapDip = 12;
        static constexpr int MaxMarginDip = 32;

        ArrangeSettings Arrange;
        FrameSettings Frames;
        std::vector<Rule> Rules;

        static std::wstring DefaultDirectory();
        static std::wstring DefaultPath();

        /// False on a first run, which is when the panel opens by itself.
        static bool ExistsAtDefaultPath();

        static AppSettings LoadDefault();
        static AppSettings Load(std::wstring const& path);

        /// Writes via a temporary file, so a crash mid-save cannot truncate
        /// the settings. Returns false if nothing could be written.
        bool Save(std::optional<std::wstring> const& path = std::nullopt) const;

        static AppSettings FromJson(Json::Value const& root);
        Json::Value ToJson() const;
    };
}
