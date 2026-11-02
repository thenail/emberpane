#include "pch.h"
#include "Native/Shell.h"
#include "Settings.h"

#include <appmodel.h>
#include <winrt/Windows.ApplicationModel.h>

#pragma comment(lib, "version.lib")

namespace EmberPane::Native::Shell
{
    namespace
    {
        constexpr wchar_t PersonalizeKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)";
        constexpr wchar_t RunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
        constexpr wchar_t RunValue[] = L"EmberPane";
        /// The TaskId in packaging\store\Package.appxmanifest.
        constexpr wchar_t StartupTaskId[] = L"EmberPane";

        bool IsDark(wchar_t const* value)
        {
            DWORD data = 1, size = sizeof(data);
            LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, PersonalizeKey, value, RRF_RT_REG_DWORD, nullptr,
                                          &data, &size);
            return status == ERROR_SUCCESS && data == 0;
        }
    }

    bool TaskbarIsDark() { return IsDark(L"SystemUsesLightTheme"); }

    bool AppsAreDark() { return IsDark(L"AppsUseLightTheme"); }

    std::wstring ExecutablePath()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length < path.size())
            {
                path.resize(length);
                return path;
            }
            path.resize(path.size() * 2);
        }
    }

    bool IsPackaged()
    {
        UINT32 length = 0;
        return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    }

    std::wstring LogDirectory()
    {
        static std::wstring const directory = []
        {
            if (IsPackaged())
            {
                auto folder = Core::AppSettings::DefaultDirectory();
                CreateDirectoryW(folder.c_str(), nullptr);
                return folder + L"\\";
            }

            auto exe = ExecutablePath();
            return exe.substr(0, exe.find_last_of(L'\\') + 1);
        }();
        return directory;
    }

    std::wstring Version()
    {
        std::wstring path = ExecutablePath();
        DWORD size = GetFileVersionInfoSizeW(path.c_str(), nullptr);
        if (size == 0) return {};

        std::vector<BYTE> data(size);
        VS_FIXEDFILEINFO* info{};
        UINT length{};
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
            !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
            return {};

        // Three parts, as releases are tagged; the fourth is always 0.
        return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." +
               std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"." +
               std::to_wstring(HIWORD(info->dwFileVersionLS));
    }

    namespace
    {
        using winrt::Windows::ApplicationModel::StartupTask;
        using winrt::Windows::ApplicationModel::StartupTaskState;

        bool IsOn(StartupTaskState state)
        {
            return state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy;
        }

        bool RunKeyEnabled()
        {
            wchar_t command[2048]{};
            DWORD size = sizeof(command);
            if (RegGetValueW(HKEY_CURRENT_USER, RunKey, RunValue, RRF_RT_REG_SZ, nullptr, command, &size) != ERROR_SUCCESS)
                return false;

            std::wstring haystack{ command };
            std::wstring needle = ExecutablePath();
            auto lower = [](std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(towlower(c)); return s; };
            return lower(haystack).find(lower(needle)) != std::wstring::npos;
        }

        /// Returns false if the registry could not be written.
        bool SetRunKey(bool enabled)
        {
            HKEY key{};
            if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
                return false;

            LSTATUS status;
            if (enabled)
            {
                std::wstring command = L"\"" + ExecutablePath() + L"\"";
                status = RegSetValueExW(key, RunValue, 0, REG_SZ, reinterpret_cast<BYTE const*>(command.c_str()),
                                        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            }
            else
            {
                status = RegDeleteValueW(key, RunValue);
                if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
            }

            RegCloseKey(key);
            return status == ERROR_SUCCESS;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> StartupEnabledAsync()
    {
        if (!IsPackaged())
            co_return RunKeyEnabled();

        try
        {
            auto task = co_await StartupTask::GetAsync(StartupTaskId);
            co_return IsOn(task.State());
        }
        catch (winrt::hresult_error const&)
        {
            co_return false;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> SetStartupEnabledAsync(bool enabled)
    {
        if (!IsPackaged())
            co_return SetRunKey(enabled) ? enabled : RunKeyEnabled();

        try
        {
            auto task = co_await StartupTask::GetAsync(StartupTaskId);
            if (enabled)
                co_return IsOn(co_await task.RequestEnableAsync());

            // Only a task the app itself enabled is the app's to disable;
            // one enabled by policy stays on.
            if (task.State() == StartupTaskState::Enabled)
            {
                task.Disable();
                co_return false;
            }
            co_return IsOn(task.State());
        }
        catch (winrt::hresult_error const&)
        {
            co_return false;
        }
    }
}
