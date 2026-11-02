#pragma once

#include <string>

#include <winrt/Windows.Foundation.h>

/// Small reads and writes against the Windows shell's own settings.
namespace EmberPane::Native::Shell
{
    /// The taskbar, Start and the tray follow "Windows mode"; application
    /// windows follow "App mode". They are separate settings, and picking the
    /// wrong one puts a white glyph on a light taskbar, where it vanishes.
    /// Absent or unreadable counts as light, Windows' own default.
    bool TaskbarIsDark();
    bool AppsAreDark();

    /// This executable's full path.
    std::wstring ExecutablePath();

    /// True when running from an MSIX package - the Microsoft Store build -
    /// rather than from the installer's or the zip's plain folder.
    bool IsPackaged();

    /// Where the logs go - trace.txt, diagnostics.txt and the probes'
    /// reports - with a trailing backslash. Beside the exe, where they are
    /// easy to find; but a package's install folder is read-only, so the
    /// Store build keeps them beside its settings instead.
    std::wstring LogDirectory();

    /// This executable's version, as "1.5.1", from its own VERSIONINFO - the
    /// one place the number is kept. Empty if it cannot be read.
    std::wstring Version();

    /// Run-at-login. Unpackaged, via the per-user Run key: no elevation, the
    /// place users expect to find it in Task Manager's Startup tab, and
    /// removable there. Packaged, via the package's StartupTask (declared in
    /// packaging\store\Package.appxmanifest): a package's own registry writes
    /// are private to it, so a Run value it wrote would never be seen.
    ///
    /// Unpackaged, true only when the entry exists AND points at this
    /// executable - a stale entry from another install location would not
    /// launch this build.
    winrt::Windows::Foundation::IAsyncOperation<bool> StartupEnabledAsync();

    /// Returns whether run-at-login is on afterwards, which is not what was
    /// asked for when the registry could not be written, or when the user
    /// has turned a packaged app's startup off in Windows itself - only they
    /// can turn it back on, there.
    winrt::Windows::Foundation::IAsyncOperation<bool> SetStartupEnabledAsync(bool enabled);
}
