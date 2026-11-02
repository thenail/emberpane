#include "pch.h"
#include "Managed/CpuSampler.h"
#include "Managed/Timing.h"
#include "Native/ProcessTree.h"

namespace EmberPane::Managed
{
    namespace
    {
        /// Five ticks: every 10 s. A new child is missed for at most that long.
        constexpr int TicksBetweenRebuilds = 5;

        winrt::handle Open(DWORD pid)
        {
            return winrt::handle{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) };
        }
    }

    CpuSampler::CpuSampler()
    {
        SetWindowSeconds(10);
    }

    void CpuSampler::SetWindowSeconds(int seconds)
    {
        m_meter.SetWindowTicks((seconds + SampleSeconds - 1) / SampleSeconds);
    }

    void CpuSampler::SetRoots(std::vector<DWORD> const& pids)
    {
        if (pids == m_roots) return;

        m_roots = pids;
        m_ticksUntilRebuild = 0;
    }

    void CpuSampler::Rebuild()
    {
        // Opened as the walk reaches them, and kept if they turn out to matter.
        std::map<DWORD, winrt::handle> opened;
        std::map<DWORD, std::uint64_t> created;

        auto creationOf = [&](std::uint32_t pid) -> std::optional<std::uint64_t>
        {
            if (auto known = created.find(pid); known != created.end())
                return known->second;

            auto process = Open(pid);
            if (!process) return std::nullopt;

            auto times = Native::ProcessTree::TimesOf(process.get());
            if (!times) return std::nullopt;

            created[pid] = times->Created;
            opened[pid] = std::move(process);
            return times->Created;
        };

        m_rootKeys.clear();
        std::vector<Core::ProcessKey> roots;
        for (DWORD pid : m_roots)
        {
            if (auto time = creationOf(pid))
            {
                Core::ProcessKey key{ pid, *time };
                m_rootKeys[pid] = key;
                roots.push_back(key);
            }
        }

        m_families = Core::BuildFamilies(Native::ProcessTree::Snapshot(), roots, creationOf);

        std::map<Core::ProcessKey, winrt::handle> handles;
        for (auto const& [root, members] : m_families)
        {
            for (auto const& key : members)
            {
                if (auto kept = m_handles.find(key); kept != m_handles.end())
                    handles[key] = std::move(kept->second);
                else if (auto fresh = opened.find(key.Pid); fresh != opened.end())
                    handles[key] = std::move(fresh->second);
            }
        }

        m_handles = std::move(handles);
    }

    bool CpuSampler::Tick()
    {
        bool rebuilt = m_ticksUntilRebuild-- <= 0;
        if (rebuilt)
        {
            Rebuild();
            m_ticksUntilRebuild = TicksBetweenRebuilds - 1;
        }

        std::map<Core::ProcessKey, std::vector<Core::ProcessSample>> samples;
        for (auto const& [root, members] : m_families)
        {
            auto& family = samples[root];
            for (auto const& key : members)
            {
                auto handle = m_handles.find(key);
                if (handle == m_handles.end()) continue;

                if (auto times = Native::ProcessTree::TimesOf(handle->second.get()))
                    family.push_back({ key, times->Cpu });
            }
        }

        m_meter.Tick(GetTickCount64() / 1000.0, samples);
        return rebuilt;
    }

    void CpuSampler::ClearWindow()
    {
        m_meter.ClearWindow();
    }

    Core::Usage CpuSampler::For(DWORD pid) const
    {
        auto key = m_rootKeys.find(pid);
        return key != m_rootKeys.end() ? m_meter.For(key->second) : Core::Usage{};
    }
}
