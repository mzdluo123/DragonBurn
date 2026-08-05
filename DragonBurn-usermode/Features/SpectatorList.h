#pragma once
#include "..\Game\Entity.h"
#include <unordered_set>

namespace SpecList
{
    struct SpecData
    {
        std::unordered_set<std::string> current_spectators;
        uintptr_t spectated_pawn;
        bool needs_refresh;
        uintptr_t prev_target_pawn = 0;
    };

    static SpecData g_spec_data;

    void GetSpectatorList(
        const std::span<const std::pair<int, CEntity>> allEntities,
        const CEntity& localEntity)
    {
        const auto previousSpectators = g_spec_data.current_spectators;
        g_spec_data.current_spectators.clear();
        g_spec_data.spectated_pawn = 0;
        g_spec_data.prev_target_pawn = 0;

        std::vector<size_t> candidateIndices;
        std::vector<MemoryReadRequest> pawnHandleRequests;
        for (size_t index = 0; index < allEntities.size(); ++index)
        {
            const CEntity& entity = allEntities[index].second;
            if (entity.Controller.Address == 0 || entity.Controller.PlayerName.empty() ||
                entity.Controller.Address == localEntity.Controller.Address ||
                (localEntity.IsAlive() && entity.IsAlive()))
            {
                continue;
            }

            candidateIndices.push_back(index);
            pawnHandleRequests.emplace_back(
                entity.Controller.Address + Offset.PlayerController.m_hPawn,
                sizeof(DWORD));
        }

        std::vector<DWORD64> candidatePawns(candidateIndices.size(), 0);
        if (!pawnHandleRequests.empty())
        {
            std::vector<DWORD> pawnHandles(pawnHandleRequests.size(), 0);
            std::vector<std::uint8_t> handleSucceeded(pawnHandleRequests.size(), 0);
            const MemoryBatchReadResult handleResult = memoryManager.BatchReadMemoryBestEffort(
                pawnHandleRequests,
                std::as_writable_bytes(std::span{ pawnHandles }),
                MemoryReadPolicy::BypassDataCache,
                handleSucceeded);
            if (handleResult.completed)
            {
                for (size_t index = 0; index < pawnHandles.size(); ++index)
                {
                    if (handleSucceeded[index] == 0)
                        pawnHandles[index] = 0;
                }

                std::vector<std::uint8_t> resolveSucceeded(pawnHandles.size(), 0);
                const MemoryBatchReadResult resolveResult = gGame.ResolveEntityHandles(
                    pawnHandles,
                    candidatePawns,
                    resolveSucceeded);
                for (size_t index = 0; index < candidatePawns.size(); ++index)
                {
                    if (handleSucceeded[index] == 0)
                        continue;
                    if (!resolveResult.completed || resolveSucceeded[index] == 0)
                        candidatePawns[index] = allEntities[candidateIndices[index]].second.Pawn.Address;
                }
            }
        }

        struct ObserverSubject
        {
            DWORD64 pawnAddress;
            size_t candidateIndex;
            bool local;
        };
        std::vector<ObserverSubject> subjects;
        subjects.reserve(candidatePawns.size() + 1);
        for (size_t index = 0; index < candidatePawns.size(); ++index)
        {
            if (candidatePawns[index] != 0)
                subjects.push_back({ candidatePawns[index], index, false });
        }
        if (!localEntity.IsAlive() && localEntity.Pawn.Address != 0)
            subjects.push_back({ localEntity.Pawn.Address, 0, true });

        if (!subjects.empty())
        {
            std::vector<MemoryReadRequest> serviceRequests;
            serviceRequests.reserve(subjects.size());
            for (const ObserverSubject& subject : subjects)
            {
                serviceRequests.emplace_back(
                    subject.pawnAddress + Offset.PlayerController.m_pObserverServices,
                    sizeof(DWORD64));
            }

            std::vector<DWORD64> observerServices(subjects.size(), 0);
            std::vector<std::uint8_t> serviceSucceeded(subjects.size(), 0);
            const MemoryBatchReadResult serviceResult = memoryManager.BatchReadMemoryBestEffort(
                serviceRequests,
                std::as_writable_bytes(std::span{ observerServices }),
                MemoryReadPolicy::BypassDataCache,
                serviceSucceeded);
            if (serviceResult.completed)
            {
                std::vector<MemoryReadRequest> targetHandleRequests;
                std::vector<size_t> targetSubjectIndices;
                for (size_t index = 0; index < observerServices.size(); ++index)
                {
                    if (serviceSucceeded[index] == 0 || observerServices[index] == 0)
                        continue;
                    targetHandleRequests.emplace_back(
                        observerServices[index] + Offset.PlayerController.m_hObserverTarget,
                        sizeof(DWORD));
                    targetSubjectIndices.push_back(index);
                }

                if (!targetHandleRequests.empty())
                {
                    std::vector<DWORD> targetHandles(targetHandleRequests.size(), 0);
                    std::vector<std::uint8_t> targetHandleSucceeded(targetHandleRequests.size(), 0);
                    const MemoryBatchReadResult targetHandleResult = memoryManager.BatchReadMemoryBestEffort(
                        targetHandleRequests,
                        std::as_writable_bytes(std::span{ targetHandles }),
                        MemoryReadPolicy::BypassDataCache,
                        targetHandleSucceeded);
                    if (targetHandleResult.completed)
                    {
                        for (size_t index = 0; index < targetHandles.size(); ++index)
                        {
                            if (targetHandleSucceeded[index] == 0)
                                targetHandles[index] = 0;
                        }

                        std::vector<DWORD64> targets(targetHandles.size(), 0);
                        std::vector<std::uint8_t> targetSucceeded(targetHandles.size(), 0);
                        const MemoryBatchReadResult targetResult = gGame.ResolveEntityHandles(
                            targetHandles,
                            targets,
                            targetSucceeded);
                        if (targetResult.completed)
                        {
                            for (size_t index = 0; index < targets.size(); ++index)
                            {
                                if (targetSucceeded[index] == 0)
                                    continue;
                                const ObserverSubject& subject = subjects[targetSubjectIndices[index]];
                                if (subject.local)
                                {
                                    g_spec_data.prev_target_pawn = targets[index];
                                    g_spec_data.spectated_pawn = targets[index];
                                }
                            }
                            for (size_t index = 0; index < targets.size(); ++index)
                            {
                                if (targetSucceeded[index] == 0)
                                    continue;
                                const ObserverSubject& subject = subjects[targetSubjectIndices[index]];
                                if (subject.local)
                                    continue;
                                if (targets[index] == localEntity.Pawn.Address ||
                                    (g_spec_data.spectated_pawn != 0 &&
                                        targets[index] == g_spec_data.spectated_pawn))
                                {
                                    g_spec_data.current_spectators.insert(
                                        allEntities[candidateIndices[subject.candidateIndex]].second.Controller.PlayerName);
                                }
                            }
                        }
                    }
                }
            }
        }

        g_spec_data.needs_refresh =
            g_spec_data.current_spectators != previousSpectators;
    }

    void SpectatorWindowList(CEntity& LocalEntity)
    {
        if (!MiscCFG::SpecList || (LocalEntity.Pawn.TeamID == 0 && !MenuConfig::ShowMenu))//&& g_spec_data.current_spectators.empty()
            return;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

        static float fontHeight = ImGui::GetFontSize();
        float requiredHeight = 0.0f;
        
        requiredHeight = g_spec_data.current_spectators.size() * (fontHeight + 5) + 20;

        ImGui::SetNextWindowPos(MenuConfig::SpecWinPos, ImGuiCond_Once);
        ImGui::SetNextWindowSize({ 150.0f, requiredHeight }, ImGuiCond_Always);
        ImGui::GetStyle().WindowRounding = 8.0f;

        std::string title = "Spectators";
        ImGui::Begin(title.c_str(), NULL, flags);

        if (MenuConfig::SpecWinChengePos)
        {
            ImGui::SetWindowPos(title.c_str(), MenuConfig::SpecWinPos);
            MenuConfig::SpecWinChengePos = false;
        }

        if (!g_spec_data.current_spectators.empty())
        {
            for (const auto& spectator : g_spec_data.current_spectators)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                ImGui::Text(spectator.c_str());
            }
        }

        MenuConfig::SpecWinPos = ImGui::GetWindowPos();
        ImGui::End();
    }
}