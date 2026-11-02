#include "pch.h"
#include "WindowRules.h"

namespace EmberPane::Core
{
    namespace
    {
        bool IsShellWindow(std::wstring const& className)
        {
            for (auto shell : { L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd" })
                if (className == shell) return true;
            return false;
        }

        std::wstring Lower(std::wstring text)
        {
            for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
            return text;
        }
    }

    bool Matches(Rule const& rule, WindowFacts const& facts)
    {
        if (Lower(rule.Exe) != Lower(facts.ExeName)) return false;
        if (!rule.Class.empty() && Lower(rule.Class) != Lower(facts.ClassName)) return false;
        if (!rule.Title.empty() && Lower(facts.Title).find(Lower(rule.Title)) == std::wstring::npos) return false;
        return true;
    }

    Disposition Classify(WindowFacts const& facts, std::vector<Rule> const& rules)
    {
        if (!facts.Visible || facts.OwnProcess || facts.CloakedHere) return Disposition::Ignore;
        if (IsShellWindow(facts.ClassName)) return Disposition::Ignore;
        if (facts.ToolWindow || facts.NoActivate) return Disposition::Ignore;

        // Dialogs and their kin.
        if (facts.HasOwner) return Disposition::Ignore;

        for (auto const& rule : rules)
            if (Matches(rule, facts)) return rule.Mode;

        if (!facts.HasCaption) return Disposition::Ignore;

        // It cannot be resized, so it cannot take a tile's size.
        if (!facts.Resizable) return Disposition::Float;

        return Disposition::Tile;
    }
}
