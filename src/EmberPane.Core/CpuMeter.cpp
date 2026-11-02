#include "pch.h"
#include "CpuMeter.h"

namespace EmberPane::Core
{
    namespace
    {
        constexpr double TicksPerSecond = 10'000'000.0;
    }

    bool IsNoAggregate(std::wstring const& exe)
    {
        for (auto name : { L"explorer.exe", L"svchost.exe", L"sihost.exe", L"RuntimeBroker.exe",
                           L"ApplicationFrameHost.exe" })
            if (_wcsicmp(exe.c_str(), name) == 0) return true;
        return false;
    }

    Families BuildFamilies(std::vector<ProcessEntry> const& entries, std::vector<ProcessKey> const& roots,
                           std::function<std::optional<std::uint64_t>(std::uint32_t)> const& creationOf)
    {
        std::map<std::uint32_t, std::vector<ProcessEntry const*>> children;
        std::map<std::uint32_t, ProcessEntry const*> byPid;
        for (auto const& entry : entries)
        {
            children[entry.Parent].push_back(&entry);
            byPid[entry.Pid] = &entry;
        }

        std::set<std::uint32_t> rootPids;
        for (auto const& root : roots)
            rootPids.insert(root.Pid);

        Families families;

        // Across families: a reused pid can make a loop of parent links, and
        // no process can count twice.
        std::set<std::uint32_t> visited(rootPids);

        for (auto const& root : roots)
        {
            auto& family = families[root];
            family.push_back(root);

            auto self = byPid.find(root.Pid);
            if (self != byPid.end() && IsNoAggregate(self->second->Exe))
                continue;

            std::vector<ProcessKey> pending{ root };
            while (!pending.empty())
            {
                auto parent = pending.back();
                pending.pop_back();

                auto found = children.find(parent.Pid);
                if (found == children.end()) continue;

                for (auto child : found->second)
                {
                    if (IsNoAggregate(child->Exe) || !visited.insert(child->Pid).second)
                        continue;

                    auto created = creationOf(child->Pid);
                    if (!created || *created < parent.Created)
                        continue;

                    ProcessKey key{ child->Pid, *created };
                    family.push_back(key);
                    pending.push_back(key);
                }
            }
        }

        return families;
    }

    void CpuMeter::SetWindowTicks(int ticks)
    {
        m_windowTicks = std::max(1, ticks);

        for (auto& [root, family] : m_families)
            while (family.Window.size() > static_cast<size_t>(m_windowTicks))
                family.Window.pop_front();
    }

    void CpuMeter::Tick(double now, std::map<ProcessKey, std::vector<ProcessSample>> const& families)
    {
        std::map<ProcessKey, std::uint64_t> latest;
        std::map<ProcessKey, Family> next;

        for (auto const& [root, samples] : families)
        {
            auto previous = m_families.find(root);
            Family family = previous != m_families.end() ? std::move(previous->second) : Family{};

            std::uint64_t delta = 0;
            for (auto const& sample : samples)
            {
                latest[sample.Key] = sample.CpuTime;

                auto last = m_lastCpu.find(sample.Key);
                if (last == m_lastCpu.end())
                {
                    // Seen for the first time: its past is part of the total,
                    // but none of it happened in this interval.
                    family.Total += sample.CpuTime;
                    continue;
                }

                if (sample.CpuTime > last->second)
                {
                    delta += sample.CpuTime - last->second;
                    family.Total += sample.CpuTime - last->second;
                }
            }

            if (m_lastTick && family.Seen)
            {
                family.Window.push_back({ now - *m_lastTick, delta });
                while (family.Window.size() > static_cast<size_t>(m_windowTicks))
                    family.Window.pop_front();
            }

            family.Seen = true;
            next[root] = std::move(family);
        }

        m_families = std::move(next);
        m_lastCpu = std::move(latest);
        m_lastTick = now;
    }

    void CpuMeter::ClearWindow()
    {
        m_lastTick.reset();

        for (auto& [root, family] : m_families)
            family.Window.clear();
    }

    Usage CpuMeter::For(ProcessKey const& root) const
    {
        auto found = m_families.find(root);
        if (found == m_families.end()) return {};

        auto const& family = found->second;

        double seconds = 0;
        std::uint64_t delta = 0;
        for (auto const& interval : family.Window)
        {
            seconds += interval.Seconds;
            delta += interval.Delta;
        }

        Usage usage;
        usage.TotalSeconds = family.Total / TicksPerSecond;
        usage.Known = seconds > 0;
        usage.Rate = usage.Known ? (delta / TicksPerSecond) / seconds : 0;
        return usage;
    }
}
