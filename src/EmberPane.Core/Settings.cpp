#include "pch.h"
#include "Settings.h"

#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace EmberPane::Core
{
    namespace
    {
        std::wstring RoamingAppData()
        {
            PWSTR path = nullptr;
            std::wstring result;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
                result = path;
            CoTaskMemFree(path);
            return result;
        }

        bool FileExists(std::wstring const& path)
        {
            DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }

        int ReadInt(Json::Value const* object, wchar_t const* name, int fallback)
        {
            if (!object) return fallback;
            auto value = object->Find(name);
            return value && value->AsInt() ? *value->AsInt() : fallback;
        }

        bool ReadBool(Json::Value const* object, wchar_t const* name, bool fallback)
        {
            if (!object) return fallback;
            auto value = object->Find(name);
            return value && value->AsBool() ? *value->AsBool() : fallback;
        }

        std::wstring ReadString(Json::Value const* object, wchar_t const* name)
        {
            if (!object) return {};
            auto value = object->Find(name);
            return value ? value->AsString().value_or(L"") : L"";
        }

        CornerStyle ParseCorners(std::wstring const& text, CornerStyle fallback)
        {
            if (text == L"Round") return CornerStyle::Round;
            if (text == L"RoundSmall") return CornerStyle::RoundSmall;
            if (text == L"Leave") return CornerStyle::Leave;
            return fallback;
        }

        wchar_t const* ToString(CornerStyle corners)
        {
            switch (corners)
            {
            case CornerStyle::RoundSmall: return L"RoundSmall";
            case CornerStyle::Leave: return L"Leave";
            default: return L"Round";
            }
        }

        constexpr std::pair<LayoutPreset, wchar_t const*> LayoutNames[] = {
            { LayoutPreset::OneAtATime, L"OneAtATime" },
            { LayoutPreset::TwoColumns, L"TwoColumns" },
            { LayoutPreset::ThreeColumns, L"ThreeColumns" },
            { LayoutPreset::LargeAndSmall, L"LargeAndSmall" },
            { LayoutPreset::LargeAndTwoSmall, L"LargeAndTwoSmall" },
            { LayoutPreset::FourAreas, L"FourAreas" },
            { LayoutPreset::LargeInTheMiddle, L"LargeInTheMiddle" },
        };

        LayoutPreset ParseLayout(std::wstring const& text, LayoutPreset fallback)
        {
            for (auto const& [preset, name] : LayoutNames)
                if (text == name) return preset;
            return fallback;
        }

        wchar_t const* ToString(LayoutPreset layout)
        {
            for (auto const& [preset, name] : LayoutNames)
                if (preset == layout) return name;
            return L"TwoColumns";
        }

        constexpr std::pair<ColorTheme, wchar_t const*> ThemeNames[] = {
            { ColorTheme::Bright, L"Bright" },
            { ColorTheme::Balanced, L"Balanced" },
            { ColorTheme::Ember, L"Ember" },
            { ColorTheme::Aurora, L"Aurora" },
        };

        ColorTheme ParseTheme(std::wstring const& text, ColorTheme fallback)
        {
            for (auto const& [theme, name] : ThemeNames)
                if (text == name) return theme;
            return fallback;
        }

        wchar_t const* ToString(ColorTheme value)
        {
            for (auto const& [theme, name] : ThemeNames)
                if (theme == value) return name;
            return L"Bright";
        }

        std::optional<Disposition> ParseMode(std::wstring const& text)
        {
            if (text == L"Tile") return Disposition::Tile;
            if (text == L"Float") return Disposition::Float;
            if (text == L"Ignore") return Disposition::Ignore;
            return std::nullopt;
        }

        wchar_t const* ToString(Disposition mode)
        {
            switch (mode)
            {
            case Disposition::Tile: return L"Tile";
            case Disposition::Ignore: return L"Ignore";
            default: return L"Float";
            }
        }
    }

    std::wstring AppSettings::DefaultDirectory()
    {
        return RoamingAppData() + L"\\EmberPane";
    }

    std::wstring AppSettings::DefaultPath()
    {
        return DefaultDirectory() + L"\\settings.json";
    }

    bool AppSettings::ExistsAtDefaultPath()
    {
        return FileExists(DefaultPath());
    }

    AppSettings AppSettings::LoadDefault()
    {
        return Load(DefaultPath());
    }

    AppSettings AppSettings::Load(std::wstring const& path)
    {
        auto root = Json::ReadFile(path);

        // Missing, unreadable or malformed: start clean rather than fail to
        // launch.
        if (!root) return {};

        return FromJson(*root);
    }

    AppSettings AppSettings::FromJson(Json::Value const& root)
    {
        AppSettings settings;

        if (auto arrange = root.Find(L"Arrange"))
        {
            auto& a = settings.Arrange;
            a.Enabled = ReadBool(arrange, L"Enabled", a.Enabled);
            a.Layout = ParseLayout(ReadString(arrange, L"Layout"), a.Layout);
            a.GapDip = std::clamp(ReadInt(arrange, L"GapDip", a.GapDip), 0, MaxGapDip);
            a.MarginDip = std::clamp(ReadInt(arrange, L"MarginDip", a.MarginDip), 0, MaxMarginDip);
            a.Corners = ParseCorners(ReadString(arrange, L"Corners"), a.Corners);
            a.GapLine = ReadBool(arrange, L"GapLine", a.GapLine);
        }

        if (auto frames = root.Find(L"Frames"))
        {
            auto& f = settings.Frames;
            f.Enabled = ReadBool(frames, L"Enabled", f.Enabled);

            int seconds = ReadInt(frames, L"WindowSeconds", f.WindowSeconds);
            if (seconds == 5 || seconds == 10 || seconds == 30)
                f.WindowSeconds = seconds;

            f.Theme = ParseTheme(ReadString(frames, L"Theme"), f.Theme);
            f.ColorPx = std::clamp(ReadInt(frames, L"ColorPx", f.ColorPx), 1, MaxFrameColorPx);
            f.InnerPx = std::clamp(ReadInt(frames, L"InnerPx", f.InnerPx), 0, MaxFrameInnerPx);
            f.ClearInner = ReadBool(frames, L"ClearInner", f.ClearInner);
        }

        if (auto rules = root.Find(L"Rules"))
        {
            for (auto const& item : rules->Items())
            {
                // A rule that names no app, or no mode we know, says nothing.
                auto mode = ParseMode(ReadString(&item, L"Mode"));
                auto exe = ReadString(&item, L"Exe");
                if (!mode || exe.empty()) continue;

                settings.Rules.push_back({ exe, ReadString(&item, L"Class"), ReadString(&item, L"Title"), *mode });
            }
        }

        return settings;
    }

    Json::Value AppSettings::ToJson() const
    {
        auto arrange = Json::Value::MakeObject();
        arrange.Set(L"Enabled", Arrange.Enabled);
        arrange.Set(L"Layout", ToString(Arrange.Layout));
        arrange.Set(L"GapDip", Arrange.GapDip);
        arrange.Set(L"MarginDip", Arrange.MarginDip);
        arrange.Set(L"Corners", ToString(Arrange.Corners));
        arrange.Set(L"GapLine", Arrange.GapLine);

        auto frames = Json::Value::MakeObject();
        frames.Set(L"Enabled", Frames.Enabled);
        frames.Set(L"WindowSeconds", Frames.WindowSeconds);
        frames.Set(L"Theme", ToString(Frames.Theme));
        frames.Set(L"ColorPx", Frames.ColorPx);
        frames.Set(L"InnerPx", Frames.InnerPx);
        frames.Set(L"ClearInner", Frames.ClearInner);

        auto rules = Json::Value::MakeArray();
        for (auto const& rule : Rules)
        {
            auto entry = Json::Value::MakeObject();
            entry.Set(L"Exe", rule.Exe);
            if (!rule.Class.empty()) entry.Set(L"Class", rule.Class);
            if (!rule.Title.empty()) entry.Set(L"Title", rule.Title);
            entry.Set(L"Mode", ToString(rule.Mode));
            rules.Append(std::move(entry));
        }

        auto root = Json::Value::MakeObject();
        root.Set(L"Arrange", std::move(arrange));
        root.Set(L"Frames", std::move(frames));
        root.Set(L"Rules", std::move(rules));
        return root;
    }

    bool AppSettings::Save(std::optional<std::wstring> const& path) const
    {
        // Losing a preference is preferable to crashing the tray app.
        return Json::WriteFile(path.value_or(DefaultPath()), Json::Write(ToJson()));
    }
}
