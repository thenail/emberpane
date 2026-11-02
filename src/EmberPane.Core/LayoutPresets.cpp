#include "pch.h"
#include "LayoutPresets.h"

namespace EmberPane::Core
{
    std::vector<LayoutPreset> AllPresets()
    {
        return { LayoutPreset::OneAtATime, LayoutPreset::TwoColumns, LayoutPreset::ThreeColumns,
                 LayoutPreset::LargeAndSmall, LayoutPreset::LargeAndTwoSmall, LayoutPreset::FourAreas };
    }

    LayoutTree MakeLayout(LayoutPreset preset)
    {
        constexpr double Third = 1.0 / 3.0;
        constexpr double Large = 0.62;

        LayoutTree tree;
        tree.AddFirst(1);

        switch (preset)
        {
        case LayoutPreset::OneAtATime:
            break;

        case LayoutPreset::ThreeColumns:
            tree.InsertBeside(2, 1, Edge::Right, Third);
            tree.InsertBeside(3, 2, Edge::Right);
            break;

        case LayoutPreset::LargeAndSmall:
            tree.InsertBeside(2, 1, Edge::Right, Large);
            break;

        case LayoutPreset::LargeAndTwoSmall:
            tree.InsertBeside(2, 1, Edge::Right, Large);
            tree.InsertBeside(3, 2, Edge::Bottom);
            break;

        case LayoutPreset::FourAreas:
            tree.InsertBeside(2, 1, Edge::Right);
            tree.InsertBeside(3, 1, Edge::Bottom);
            tree.InsertBeside(4, 2, Edge::Bottom);
            break;

        default:
            tree.InsertBeside(2, 1, Edge::Right);
            break;
        }

        return tree;
    }

    std::vector<Rect> PictureOf(LayoutPreset preset)
    {
        std::vector<Rect> areas;
        for (auto const& tile : MakeLayout(preset).Arrange({ 0, 0, 1000, 1000 }, 0, {}))
            areas.push_back(tile.Bounds);
        return areas;
    }
}
