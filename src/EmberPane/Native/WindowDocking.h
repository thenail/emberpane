#pragma once

/// Windows' own docking: Snap's response to a window dragged to a screen edge.
/// While arranging is on it would answer the same drag as Ember Pane's preview,
/// so it has to be off, and off before the press: changing it during a drag has
/// no effect, measured on Windows 11 build 26300.
///
/// The change is made for this session only and never written to the user's
/// profile, so that even a crash cannot lose their preference: signing out, or
/// the next start of Ember Pane, puts it back.
namespace EmberPane::Native::WindowDocking
{
    bool IsOn();

    /// What the user has chosen, as saved in their profile.
    bool Preference();

    /// False if it did not take: this reads the setting back.
    bool Set(bool on);
}
