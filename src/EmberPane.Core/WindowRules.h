#pragma once

#include <string>
#include <vector>
#include "Settings.h"

namespace EmberPane::Core
{
    /// What is known about a top-level window, read once by Native/WindowFacts
    /// so that this decision can be tested without a window.
    struct WindowFacts
    {
        bool Visible{ false };
        bool HasOwner{ false };
        bool ToolWindow{ false };
        bool NoActivate{ false };
        bool HasCaption{ false };
        bool Resizable{ false };

        /// Ours: the host's or the UI's.
        bool OwnProcess{ false };

        /// Cloaked, yet on the current desktop: usually a UWP app hiding
        /// itself. Windows on other virtual desktops are cloaked too, and are
        /// not this.
        bool CloakedHere{ false };

        std::wstring ClassName;
        std::wstring ExeName;
        std::wstring Title;
    };

    /// Whether a rule applies: exe name, optionally narrowed by class and by a
    /// title substring, all case-insensitively.
    bool Matches(Rule const& rule, WindowFacts const& facts);

    /// First match wins. Rules come after the checks that catch what is never
    /// an app window, and before the style checks, so that a user can tile an
    /// app that draws its own frame.
    Disposition Classify(WindowFacts const& facts, std::vector<Rule> const& rules);
}
