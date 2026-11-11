#pragma once

#include <optional>
#include <string>
#include <vector>
#include "HeatRamp.h"
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

        /// A half in the middle, a quarter each side, as Windows' Snap has it.
        /// Last, since the layout state saves presets by number.
        LargeInTheMiddle,
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

        ColorTheme Theme{ ColorTheme::Bright };

        /// In real pixels, like the border they stand beside: a frame looks
        /// thin or thick by the pixel. See FramePlan.h.
        int ColorPx{ 2 };
        int InnerPx{ 2 };

        /// The space inside the frame shows what is behind it, rather than
        /// being a dark line.
        bool ClearInner{ false };
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
        static constexpr int MaxFrameColorPx = 6;
        static constexpr int MaxFrameInnerPx = 4;

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
