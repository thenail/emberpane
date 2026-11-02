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

        std::optional<std::string> ReadAllBytes(std::wstring const& path)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return std::nullopt;

            LARGE_INTEGER size{};
            std::optional<std::string> result;

            // A settings file larger than this is not a settings file.
            if (GetFileSizeEx(file, &size) && size.QuadPart < 16 * 1024 * 1024)
            {
                std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
                DWORD read = 0;
                if (::ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
                {
                    bytes.resize(read);
                    result = std::move(bytes);
                }
            }

            CloseHandle(file);
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
        auto bytes = ReadAllBytes(path);
        if (!bytes) return {};

        auto root = Json::Parse(Json::FromUtf8(*bytes));

        // Unreadable or malformed: start clean rather than fail to launch.
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
        auto target = path.value_or(DefaultPath());

        auto directory = target.substr(0, target.find_last_of(L'\\'));
        SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);

        auto temp = target + L".tmp";
        auto bytes = Json::ToUtf8(Json::Write(ToJson()));

        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
                  && written == bytes.size();
        CloseHandle(file);

        // Losing a preference is preferable to crashing the tray app.
        if (!ok)
        {
            DeleteFileW(temp.c_str());
            return false;
        }

        return MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
}
