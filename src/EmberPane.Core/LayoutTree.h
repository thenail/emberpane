#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include "Geometry.h"

namespace EmberPane::Core
{
    /// A leaf, opaquely: an area of a layout. Core knows no HWNDs, so every
    /// decision here can be tested without a window.
    using WindowId = std::uint64_t;

    /// A split in the tree, stable for as long as the split lasts.
    using SplitId = std::uint64_t;

    /// How small each window may go.
    struct MinimumSizes
    {
        Size Default;

        /// What apps turned out to refuse, learned by reading windows back.
        std::map<WindowId, Size> Learned;

        Size For(WindowId id) const;
    };

    /// A tile with the window's own insets taken off, each pair given back in
    /// proportion so that at least `minimum` is left of it. An inset never
    /// goes outside the tile.
    Rect InsetTile(Rect const& tile, Insets const& insets, Size minimum);

    /// Where a window lands: its visible rectangle, not the one given to
    /// Windows, which is larger by the invisible resize border.
    struct Tile
    {
        WindowId Id;
        Rect Bounds;
    };

    struct GapHit
    {
        SplitId Split;
        Axis Direction;

        /// The gap itself, for the gap line.
        Rect Gap;
    };

    /// The areas of one layout, as a binary tree: each split holds an axis and
    /// the first child's share of the space left after the gap. A dragged edge
    /// maps directly onto one split's ratio. Leaves are opaque ids.
    class LayoutTree
    {
    public:
        LayoutTree();
        ~LayoutTree();
        LayoutTree(LayoutTree const& other);
        LayoutTree& operator=(LayoutTree const& other);
        LayoutTree(LayoutTree&&) noexcept;
        LayoutTree& operator=(LayoutTree&&) noexcept;

        bool Empty() const;
        size_t Count() const;
        bool Contains(WindowId id) const;
        std::vector<WindowId> Windows() const;

        /// Splits the leaf holding `beside`, or without one the largest leaf
        /// (the most recently inserted, on a tie), along its longer side.
        /// False when the halves would not reach the minimum: the caller
        /// floats the window rather than squeezing it in.
        bool Insert(WindowId id, std::optional<WindowId> beside, Rect const& area, int gap,
                    MinimumSizes const& minimums);

        /// The sibling takes the parent's place.
        void Remove(WindowId id);

        /// The first leaf of an empty tree.
        void AddFirst(WindowId id);

        /// Splits `beside` to hold a new leaf, on that side. ratio is the share
        /// of the first child, whichever of the two that is.
        void InsertBeside(WindowId id, WindowId beside, Edge side, double ratio = 0.5);

        std::vector<Tile> Arrange(Rect const& area, int gap, MinimumSizes const& minimums) const;

        /// The tile under a point, in this arrangement.
        std::optional<Tile> TileAt(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums) const;

        /// The tile under a point, or the nearest one when the point is in a gap
        /// or a margin, so that a drag always has somewhere to go.
        std::optional<Tile> NearestTile(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums) const;

        /// Moves the split that bounds this edge of the window. False when the
        /// edge lies against the area's boundary, where there is no split.
        bool MoveEdge(WindowId id, Edge edge, int position, Rect const& area, int gap, MinimumSizes const& minimums);

        /// The gap under a point. Where gaps cross, the outermost split wins,
        /// since its gap is the unbroken one.
        std::optional<GapHit> GapAt(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums) const;

        /// Near a gap between two areas: in the gap itself, or within `share`
        /// of either neighbour's length across it. A window let go here is
        /// left out of the layout. The margin at the area's edge is not.
        bool NearGap(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums, double share) const;

        /// Moves a split directly, for a drag that starts on the gap: position
        /// is where the gap's first edge should go.
        void MoveSplit(SplitId split, int position, Rect const& area, int gap, MinimumSizes const& minimums);

        /// Each split's share, outermost first. With the tree's shape, that is
        /// all a resize changes, so it is what is kept across a restart.
        std::vector<double> Ratios() const;

        /// Puts back what Ratios gave, on a tree of the same shape. False, with
        /// nothing changed, when they do not fit it.
        bool SetRatios(std::vector<double> const& ratios);

        /// Defined in the .cpp, where its helpers live.
        struct Node;

    private:
        std::unique_ptr<Node> m_root;
        std::uint64_t m_nextSequence{ 1 };
        SplitId m_nextSplit{ 1 };
    };
}
