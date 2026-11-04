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
}
