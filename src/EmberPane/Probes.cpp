#include "pch.h"
#include "Probes.h"
#include "Json.h"
#include "Native/Shell.h"
#include "Native/VirtualDesktops.h"
#include "Native/WindowFacts.h"
#include "Settings.h"
#include "WindowRules.h"

#include <Psapi.h>

namespace EmberPane::Probes
{
    namespace
    {
        std::wstring LogPath(wchar_t const* name)
        {
            return Native::Shell::LogDirectory() + name;
        }

        void WriteText(wchar_t const* name, std::wstring const& text)
        {
            auto bytes = Core::Json::ToUtf8(text);
            HANDLE file = CreateFileW(LogPath(name).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;
            DWORD written = 0;
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
            CloseHandle(file);
        }

        struct Memory
        {
            size_t PrivateMb;
            size_t WorkingSetMb;
        };

        Memory SampleMemory()
        {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            counters.cb = sizeof(counters);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters));
            return { counters.PrivateUsage / (1024 * 1024), counters.WorkingSetSize / (1024 * 1024) };
        }
    }

    void WriteWindowProbe()
    {
        std::vector<HWND> windows;
        EnumWindows([](HWND hwnd, LPARAM list)
        {
            reinterpret_cast<std::vector<HWND>*>(list)->push_back(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&windows));

        auto rules = Core::AppSettings::LoadDefault().Rules;
        Native::VirtualDesktops desktops;

        auto yes = [](bool value) { return value ? L"yes" : L"no"; };

        std::wstring report = L"Every visible top-level window, frontmost first. This file includes window titles.\r\n\r\n";
        for (auto hwnd : windows)
        {
            if (!IsWindowVisible(hwnd)) continue;

            auto reading = Native::ReadWindow(hwnd, desktops);
            auto const& f = reading.Facts;

            wchar_t const* outcome = L"ignored";
            switch (Core::Classify(f, rules))
            {
            case Core::Disposition::Tile: outcome = L"arrange"; break;
            case Core::Disposition::Float: outcome = L"leave where it is"; break;
            default: break;
            }

            RECT rect{};
            GetWindowRect(hwnd, &rect);

            report += std::wstring(outcome) + L": " + f.ExeName + L" [" + f.ClassName + L"] \"" + f.Title + L"\"\r\n";
            report += L"    pid " + std::to_wstring(reading.ProcessId) + L", " +
                      std::to_wstring(rect.right - rect.left) + L" x " + std::to_wstring(rect.bottom - rect.top) +
                      L", caption " + yes(f.HasCaption) + L", resizable " + yes(f.Resizable) + L", owner " +
                      yes(f.HasOwner) + L", tool " + yes(f.ToolWindow) + L", no-activate " + yes(f.NoActivate) +
                      L", cloaked here " + yes(f.CloakedHere) + L"\r\n";
        }

        WriteText(L"windows-probe.txt", report);
    }

    void WriteDiagnostics()
    {
        OSVERSIONINFOEXW version{ sizeof(version) };
        auto rtlGetVersion = reinterpret_cast<LONG(WINAPI*)(OSVERSIONINFOEXW*)>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        if (rtlGetVersion) rtlGetVersion(&version);

        DWORD transparency = 1, size = sizeof(transparency);
        RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                     L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &transparency, &size);

        std::wstring lines;
        lines += L"command line        : " + std::wstring(GetCommandLineW()) + L"\r\n";
        lines += L"OS build            : " + std::to_wstring(version.dwBuildNumber) + L"\r\n";
        lines += std::wstring(L"transparency effects: ") + (transparency ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"taskbar dark        : ") + (Native::Shell::TaskbarIsDark() ? L"True" : L"False") + L"\r\n";
        lines += std::wstring(L"apps dark           : ") + (Native::Shell::AppsAreDark() ? L"True" : L"False") + L"\r\n";
        WriteText(L"diagnostics.txt", lines);

        // Sampled again once things have settled. The tray process's own
        // figure: the UI is a process of its own, and gone when closed.
        auto timer = winrt::Windows::System::DispatcherQueue::GetForCurrentThread().CreateTimer();
        timer.Interval(std::chrono::seconds(12));
        timer.IsRepeating(false);
        timer.Tick([lines](auto&&, auto&&) mutable
        {
            auto m = SampleMemory();
            lines += L"--- after 12s ---\r\n";
            lines += L"private bytes       : " + std::to_wstring(m.PrivateMb) + L" MB\r\n";
            lines += L"working set         : " + std::to_wstring(m.WorkingSetMb) + L" MB\r\n";
            WriteText(L"diagnostics.txt", lines);
        });
        timer.Start();

        // Deliberately leaked: a one-shot aid, and a WinRT object in a static
        // would be torn down after WinUI itself, at exit.
        winrt::detach_abi(timer);
    }
}
