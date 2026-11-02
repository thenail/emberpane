#pragma once

#include <optional>
#include <string>
#include <vector>
#include "CpuMeter.h"

/// Processes, and which of them a window really belongs to.
namespace EmberPane::Native::ProcessTree
{
    /// Every process running, from a Toolhelp snapshot.
    std::vector<Core::ProcessEntry> Snapshot();

    /// The process behind a window. UWP windows belong to
    /// ApplicationFrameHost.exe, and the app is the process of the child window
    /// that differs from it; until that child exists, the frame's own.
    DWORD OwnerOf(HWND hwnd);

    /// The process's full path; empty when it cannot be read.
    std::wstring ExePath(DWORD pid);

    /// The name a person knows the app by, "Google Chrome" for chrome.exe,
    /// from its version resource; the file name without .exe when it has none.
    std::wstring AppName(std::wstring const& exePath);

    /// Kernel plus user time, in 100 ns units.
    struct Times
    {
        std::uint64_t Created;
        std::uint64_t Cpu;
    };

    std::optional<Times> TimesOf(HANDLE process);
}
