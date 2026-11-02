#pragma once

#include "Managed/ManagedWindow.h"

/// The window's own border, drawn by DWM, so it follows the window and its
/// corners exactly, with no extra windows, no z-order to chase and no lag.
/// FrameRings decides what it shows.
///
/// Everything set here outlives this process, and neither attribute can be
/// read back: Release is the only way home, and it goes to Windows' default.
namespace EmberPane::Managed::FrameService
{
    /// Writes only when the colour differs from the last one written. A
    /// window minimized, maximized or fullscreen has no border to colour.
    void Paint(ManagedWindow& window, Core::FrameColor color);

    /// The corner preference, or Windows' own for nothing. A hint: windows
    /// using per-pixel alpha or window regions cannot be rounded.
    void Round(ManagedWindow& window, std::optional<Core::CornerStyle> corners);

    /// Gives the window back as it was found.
    void Release(ManagedWindow& window);
}
