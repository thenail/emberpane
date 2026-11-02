#include "pch.h"
#include "LayoutTree.h"

namespace EmberPane::Core
{
    struct LayoutTree::Node
    {
        Node* Parent{ nullptr };

        // A leaf.
        WindowId Id{ 0 };
        std::uint64_t Sequence{ 0 };

        // A split.
        SplitId Split{ 0 };
        Axis Direction{ Axis::SideBySide };
        double Ratio{ 0.5 };
        std::unique_ptr<Node> First;
        std::unique_ptr<Node> Second;

        bool IsLeaf() const { return !First; }

        std::unique_ptr<Node> Clone(Node* parent) const
        {
            auto copy = std::make_unique<Node>();
            copy->Parent = parent;
            copy->Id = Id;
            copy->Sequence = Sequence;
            copy->Split = Split;
            copy->Direction = Direction;
            copy->Ratio = Ratio;
            if (First)
            {
                copy->First = First->Clone(copy.get());
                copy->Second = Second->Clone(copy.get());
            }
            return copy;
        }
    };

    namespace
    {
        using Node = LayoutTree::Node;

        Node* Find(Node* node, WindowId id)
        {
            if (!node) return nullptr;
            if (node->IsLeaf()) return node->Id == id ? node : nullptr;

            if (auto found = Find(node->First.get(), id)) return found;
            return Find(node->Second.get(), id);
        }

        Node* FindSplit(Node* node, SplitId split)
        {
            if (!node || node->IsLeaf()) return nullptr;
            if (node->Split == split) return node;

            if (auto found = FindSplit(node->First.get(), split)) return found;
            return FindSplit(node->Second.get(), split);
        }

        template <typename Visit>
        void EachLeaf(Node const* node, Visit const& visit)
        {
            if (!node) return;
            if (node->IsLeaf())
            {
                visit(*node);
                return;
            }
            EachLeaf(node->First.get(), visit);
            EachLeaf(node->Second.get(), visit);
        }

        /// The smallest a subtree can go: along a split its children and the
        /// gap add up, and across it the larger child decides.
        Size MinimumOf(Node const* node, int gap, MinimumSizes const& minimums)
        {
            if (node->IsLeaf()) return minimums.For(node->Id);

            auto a = MinimumOf(node->First.get(), gap, minimums);
            auto b = MinimumOf(node->Second.get(), gap, minimums);

            if (node->Direction == Axis::SideBySide)
                return { a.Width + gap + b.Width, std::max(a.Height, b.Height) };
            return { std::max(a.Width, b.Width), a.Height + gap + b.Height };
        }

        int Along(Size size, Axis axis) { return axis == Axis::SideBySide ? size.Width : size.Height; }
        int Along(Rect const& rect, Axis axis) { return axis == Axis::SideBySide ? rect.Width() : rect.Height(); }
        int Start(Rect const& rect, Axis axis) { return axis == Axis::SideBySide ? rect.Left : rect.Top; }

        /// The first child's length. Integers, so a gap is exactly the gap.
        /// When the area cannot satisfy both minimums, each gets a share of
        /// what fits: a window asking for more than the screen has must not
        /// break the layout.
        int FirstLength(int space, double ratio, int minimumFirst, int minimumSecond)
        {
            if (minimumFirst + minimumSecond > space)
            {
                int total = std::max(1, minimumFirst + minimumSecond);
                return static_cast<int>(std::lround(static_cast<double>(space) * minimumFirst / total));
            }

            return std::clamp(static_cast<int>(std::lround(space * ratio)), minimumFirst, space - minimumSecond);
        }

        struct Parts
        {
            Rect First;
            Rect Gap;
            Rect Second;
        };

        Parts Divide(Node const& node, Rect const& rect, int gap, MinimumSizes const& minimums)
        {
            int space = std::max(0, Along(rect, node.Direction) - gap);
            int first = FirstLength(space, node.Ratio,
                                    Along(MinimumOf(node.First.get(), gap, minimums), node.Direction),
                                    Along(MinimumOf(node.Second.get(), gap, minimums), node.Direction));

            Parts parts{ rect, rect, rect };
            if (node.Direction == Axis::SideBySide)
            {
                parts.First.Right = rect.Left + first;
                parts.Gap.Left = parts.First.Right;
                parts.Gap.Right = parts.Gap.Left + gap;
                parts.Second.Left = parts.Gap.Right;
            }
            else
            {
                parts.First.Bottom = rect.Top + first;
                parts.Gap.Top = parts.First.Bottom;
                parts.Gap.Bottom = parts.Gap.Top + gap;
                parts.Second.Top = parts.Gap.Bottom;
            }
            return parts;
        }

        void Collect(Node const* node, Rect const& rect, int gap, MinimumSizes const& minimums,
                     std::vector<Tile>& tiles)
        {
            if (node->IsLeaf())
            {
                tiles.push_back({ node->Id, rect });
                return;
            }

            auto parts = Divide(*node, rect, gap, minimums);
            Collect(node->First.get(), parts.First, gap, minimums, tiles);
            Collect(node->Second.get(), parts.Second, gap, minimums, tiles);
        }

        /// The rectangle a node occupies, worked down from the root.
        Rect RectOf(Node const* node, Rect const& area, int gap, MinimumSizes const& minimums)
        {
            if (!node->Parent) return area;

            auto parent = node->Parent;
            auto parts = Divide(*parent, RectOf(parent, area, gap, minimums), gap, minimums);
            return parent->First.get() == node ? parts.First : parts.Second;
        }

        /// Sets a split so that its first child is this long, as far as the
        /// minimums allow.
        void SetFirstLength(Node& split, int length, Rect const& rect, int gap, MinimumSizes const& minimums)
        {
            int space = Along(rect, split.Direction) - gap;
            if (space <= 0) return;

            int low = Along(MinimumOf(split.First.get(), gap, minimums), split.Direction);
            int high = space - Along(MinimumOf(split.Second.get(), gap, minimums), split.Direction);
            if (low <= high)
                length = std::clamp(length, low, high);

            split.Ratio = static_cast<double>(length) / space;
        }

        Axis AxisOf(Edge edge) { return edge == Edge::Left || edge == Edge::Right ? Axis::SideBySide : Axis::Stacked; }
        bool GoesFirst(Edge side) { return side == Edge::Left || side == Edge::Top; }
    }

    Size MinimumSizes::For(WindowId id) const
    {
        auto found = Learned.find(id);
        if (found == Learned.end()) return Default;
        return { std::max(Default.Width, found->second.Width), std::max(Default.Height, found->second.Height) };
    }

    LayoutTree::LayoutTree() = default;
    LayoutTree::~LayoutTree() = default;
    LayoutTree::LayoutTree(LayoutTree&&) noexcept = default;
    LayoutTree& LayoutTree::operator=(LayoutTree&&) noexcept = default;

    LayoutTree::LayoutTree(LayoutTree const& other)
        : m_root(other.m_root ? other.m_root->Clone(nullptr) : nullptr),
          m_nextSequence(other.m_nextSequence),
          m_nextSplit(other.m_nextSplit)
    {
    }

    LayoutTree& LayoutTree::operator=(LayoutTree const& other)
    {
        if (this != &other)
        {
            m_root = other.m_root ? other.m_root->Clone(nullptr) : nullptr;
            m_nextSequence = other.m_nextSequence;
            m_nextSplit = other.m_nextSplit;
        }
        return *this;
    }

    bool LayoutTree::Empty() const
    {
        return !m_root;
    }

    size_t LayoutTree::Count() const
    {
        size_t count = 0;
        EachLeaf(m_root.get(), [&](Node const&) { ++count; });
        return count;
    }

    bool LayoutTree::Contains(WindowId id) const
    {
        return Find(m_root.get(), id) != nullptr;
    }

    std::vector<WindowId> LayoutTree::Windows() const
    {
        std::vector<WindowId> windows;
        EachLeaf(m_root.get(), [&](Node const& leaf) { windows.push_back(leaf.Id); });
        return windows;
    }

    bool LayoutTree::Insert(WindowId id, std::optional<WindowId> beside, Rect const& area, int gap,
                            MinimumSizes const& minimums)
    {
        if (Contains(id)) return true;

        if (!m_root)
        {
            AddFirst(id);
            return true;
        }

        Node* target = beside ? Find(m_root.get(), *beside) : nullptr;
        if (!target)
        {
            long long largest = -1;
            std::uint64_t latest = 0;
            auto root = m_root.get();
            EachLeaf(root, [&](Node const& leaf)
            {
                long long size = RectOf(&leaf, area, gap, minimums).Area();
                if (size > largest || (size == largest && leaf.Sequence > latest))
                {
                    largest = size;
                    latest = leaf.Sequence;
                    target = const_cast<Node*>(&leaf);
                }
            });
        }

        auto rect = RectOf(target, area, gap, minimums);
        auto axis = rect.Width() >= rect.Height() ? Axis::SideBySide : Axis::Stacked;

        // Both halves must take their windows' minimums, along the split.
        int space = Along(rect, axis) - gap;
        int half = space / 2;
        auto own = minimums.For(target->Id);
        auto incoming = minimums.For(id);
        if (half < Along(own, axis) || half < Along(incoming, axis))
            return false;

        InsertBeside(id, target->Id, axis == Axis::SideBySide ? Edge::Right : Edge::Bottom);
        return true;
    }

    void LayoutTree::AddFirst(WindowId id)
    {
        m_root = std::make_unique<Node>();
        m_root->Id = id;
        m_root->Sequence = m_nextSequence++;
    }

    void LayoutTree::InsertBeside(WindowId id, WindowId beside, Edge side, double ratio)
    {
        auto target = Find(m_root.get(), beside);
        if (!target) return;

        auto existing = std::make_unique<Node>();
        existing->Parent = target;
        existing->Id = target->Id;
        existing->Sequence = target->Sequence;

        auto added = std::make_unique<Node>();
        added->Parent = target;
        added->Id = id;
        added->Sequence = m_nextSequence++;

        target->Id = 0;
        target->Sequence = 0;
        target->Split = m_nextSplit++;
        target->Direction = AxisOf(side);
        target->Ratio = ratio;
        target->First = GoesFirst(side) ? std::move(added) : std::move(existing);
        target->Second = target->First->Id == id ? std::move(existing) : std::move(added);
    }

    void LayoutTree::Remove(WindowId id)
    {
        auto leaf = Find(m_root.get(), id);
        if (!leaf) return;

        auto parent = leaf->Parent;
        if (!parent)
        {
            m_root.reset();
            return;
        }

        // The sibling takes the parent's place, keeping its own subtree.
        auto sibling = std::move(parent->First.get() == leaf ? parent->Second : parent->First);
        sibling->Parent = parent->Parent;

        if (auto grandparent = parent->Parent)
            (grandparent->First.get() == parent ? grandparent->First : grandparent->Second) = std::move(sibling);
        else
            m_root = std::move(sibling);
    }

    std::vector<Tile> LayoutTree::Arrange(Rect const& area, int gap, MinimumSizes const& minimums) const
    {
        std::vector<Tile> tiles;
        if (m_root)
            Collect(m_root.get(), area, gap, minimums, tiles);
        return tiles;
    }

    std::optional<Tile> LayoutTree::TileAt(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums) const
    {
        for (auto const& tile : Arrange(area, gap, minimums))
            if (tile.Bounds.Contains(point))
                return tile;
        return std::nullopt;
    }

    std::optional<Tile> LayoutTree::NearestTile(Pixel point, Rect const& area, int gap,
                                                MinimumSizes const& minimums) const
    {
        std::optional<Tile> nearest;
        long long best = 0;

        for (auto const& tile : Arrange(area, gap, minimums))
        {
            long long dx = std::max({ tile.Bounds.Left - point.X, 0, point.X - (tile.Bounds.Right - 1) });
            long long dy = std::max({ tile.Bounds.Top - point.Y, 0, point.Y - (tile.Bounds.Bottom - 1) });
            long long distance = dx * dx + dy * dy;

            if (!nearest || distance < best)
            {
                nearest = tile;
                best = distance;
            }
        }

        return nearest;
    }

    bool LayoutTree::MoveEdge(WindowId id, Edge edge, int position, Rect const& area, int gap,
                              MinimumSizes const& minimums)
    {
        auto axis = AxisOf(edge);
        bool wantFirst = !GoesFirst(edge);

        auto child = Find(m_root.get(), id);
        if (!child) return false;

        // Up to the nearest split on this axis with the window on the side
        // that edge faces.
        for (auto split = child->Parent; split; child = split, split = split->Parent)
        {
            bool isFirst = split->First.get() == child;
            if (split->Direction != axis || isFirst != wantFirst) continue;

            auto rect = RectOf(split, area, gap, minimums);
            int length = wantFirst ? position - Start(rect, axis) : position - gap - Start(rect, axis);
            SetFirstLength(*split, length, rect, gap, minimums);
            return true;
        }

        return false;
    }

    std::optional<GapHit> LayoutTree::GapAt(Pixel point, Rect const& area, int gap, MinimumSizes const& minimums) const
    {
        Node const* node = m_root.get();
        Rect rect = area;

        // Down from the root: the outermost split containing the point wins.
        while (node && !node->IsLeaf())
        {
            auto parts = Divide(*node, rect, gap, minimums);
            if (parts.Gap.Contains(point))
                return GapHit{ node->Split, node->Direction, parts.Gap };

            bool inFirst = parts.First.Contains(point);
            if (!inFirst && !parts.Second.Contains(point)) return std::nullopt;

            rect = inFirst ? parts.First : parts.Second;
            node = inFirst ? node->First.get() : node->Second.get();
        }

        return std::nullopt;
    }

    void LayoutTree::MoveSplit(SplitId split, int position, Rect const& area, int gap, MinimumSizes const& minimums)
    {
        auto node = FindSplit(m_root.get(), split);
        if (!node) return;

        auto rect = RectOf(node, area, gap, minimums);
        SetFirstLength(*node, position - Start(rect, node->Direction), rect, gap, minimums);
    }
}
