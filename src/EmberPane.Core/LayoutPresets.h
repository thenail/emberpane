#pragma once

#include <vector>
#include "LayoutTree.h"
#include "Settings.h"

/// The layouts offered in the panel. Each is a few areas, and however many
/// windows are open, there are never more places than that on screen: windows
/// beyond the number of areas share one, in front of each other.
namespace EmberPane::Core
{
    /// Every preset, in the order the panel shows them.
    std::vector<LayoutPreset> AllPresets();

    /// The areas as a tree whose leaves are area numbers, from 1, not windows.
    /// Keeping them in a LayoutTree is what lets a dragged edge or the gap line
    /// resize them.
    LayoutTree MakeLayout(LayoutPreset preset);

    /// The areas on a 1000 by 1000 square, for drawing the panel's pictures.
    std::vector<Rect> PictureOf(LayoutPreset preset);

    /// The same for areas as they stand, so a resized layout draws resized.
    std::vector<Rect> PictureOf(LayoutTree const& areas);

    /// The areas have been resized away from the preset's: some edge has moved
    /// by more than a sliver, measured on the picture's square.
    bool IsResized(LayoutTree const& areas, LayoutPreset preset);

    /// Where a window that Windows' Snap has just placed belongs.
    struct SnapTarget
    {
        LayoutPreset Preset;
        WindowId Area;
    };

    /// `snapped` is the window's visible rectangle and `work` the work area of
    /// its monitor, in pixels; `current` and `areas` are the layout in use
    /// there, as it stands. Snap says only where the window went, not which of
    /// its layouts was picked, and the same place is in several of them, so:
    ///
    /// 1. An area of the layout in use, as resized, that is the same place.
    /// 2. Otherwise a place in one of Snap's layouts, each a preset at
    ///    Windows' sizes. The layout in use if it has that place, else the
    ///    preset with the fewest areas that does.
    /// 3. Otherwise the area of the layout in use holding most of the window,
    ///    as for a monitor stood on end, whose layouts Snap stacks.
    ///
    /// None when no area holds most of it.
    std::optional<SnapTarget> MatchSnap(Rect const& snapped, Rect const& work, LayoutPreset current,
                                        LayoutTree const& areas);

    /// Several windows Snap placed together on one monitor, as it does for
    /// the layouts it suggests, filled with windows: one preset for them all.
    struct SnapGroup
    {
        LayoutPreset Preset;

        /// In the order given; none for a window no area of it holds.
        std::vector<std::optional<WindowId>> Areas;
    };

    /// The first window is the one the user chose. The layout in use if it has
    /// a place for every window, as it stands or as Snap has it; otherwise the
    /// preset with the fewest areas that does. Failing both, the first window
    /// decides, as MatchSnap, and the others go where that preset has their
    /// place. None when the first has no place at all.
    std::optional<SnapGroup> MatchSnaps(std::vector<Rect> const& snapped, Rect const& work, LayoutPreset current,
                                        LayoutTree const& areas);
}
