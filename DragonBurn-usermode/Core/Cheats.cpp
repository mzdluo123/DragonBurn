//______                            ______                  
//|  _  \                           | ___ \                 
//| | | |_ __ __ _  __ _  ___  _ __ | |_/ /_   _ _ __ _ __  
//| | | | '__/ _` |/ _` |/ _ \| '_ \| ___ \ | | | '__| '_ \ 
//| |/ /| | | (_| | (_| | (_) | | | | |_/ / |_| | |  | | | |
//|___/ |_|  \__,_|\__, |\___/|_| |_\____/ \__,_|_|  |_| |_|
//                  __/ |                                   
//                 |___/                                    
//
//https://discord.gg/5WcvdzFybD
//https://github.com/ByteCorum/DragonBurn

#include <string>
#include <thread>
#include <cmath>

#include "Cheats.h"
#include "Render.h"
#include "../Core/Config.h"

#include "../Core/Init.h"

#include "../Features/ESP.h"
#include "../Core/GUI.h"
#include "../Features/RCS.H"
#include "../Features/BombTimer.h"
#include "../Features/SpectatorList.h"
#include "../Features/SoundESP.h"
#include "../Features/WebRadar.h"

int PreviousTotalHits = 0;

void Menu();
void Visual(const CEntity&);
void Radar(Base_Radar, const CEntity&);
void Trigger(const CEntity&, const int&);
void AIM(const CEntity&, const std::vector<AimControl::AimCandidate>&);
void MiscFuncs(CEntity&, bool);
void RenderCrosshair(ImDrawList*, const CEntity&);
void RadarSetting(Base_Radar&);

namespace
{
	struct EntitySnapshot
	{
		DWORD tick = 0;
		UINT64 entityListGeneration = 0;
		std::string mapName;
		DWORD64 localControllerAddress = 0;
		DWORD64 localPawnAddress = 0;
		CEntity localEntity;
		int localPlayerControllerIndex = -1;
		std::vector<std::pair<int, CEntity>> entities;
		bool foregroundReady = false;
		bool valid = false;
	};

	DWORD m_currentTick = 0;
	DWORD previousFeatureTick = 0;
	EntitySnapshot entitySnapshot;
	std::vector<std::pair<int, CEntity>> featureEntityCache;

	void InvalidateEntityState()
	{
		entitySnapshot = {};
		featureEntityCache.clear();
		previousFeatureTick = 0;
		gGame.InvalidateEntityListCache();
		WebRadar::Invalidate();
		AimControl::ResetRuntime();
		RCS::ResetRuntime();
	}
}

void Cheats::Run()
{
	Menu();

	const bool allowGameInput = memoryManager.GetBackendKind() == MemoryBackendKind::Driver;
	if (allowGameInput)
		Misc::AutoAccept::UpdateAutoAccept();
	else
	{
		AimControl::ResetRuntime();
		RCS::ResetRuntime();
	}

	const HWND foregroundWindow = GetForegroundWindow();
	const bool backgroundRadarOnly = allowGameInput
		? foregroundWindow != Init::Client::GetGameWindow() && foregroundWindow != Gui.Window.hWnd
		: foregroundWindow != Gui.Window.hWnd;
	const bool matrixReady =
		memoryManager.ReadMemory(gGame.GetMatrixAddress(), gGame.View.Matrix, 64);

	DWORD64 localControllerAddress = 0;
	DWORD64 localPawnAddress = 0;
	memoryManager.ReadMemory(gGame.GetLocalControllerAddress(), localControllerAddress);
	memoryManager.ReadMemory(gGame.GetLocalPawnAddress(), localPawnAddress);
	if (localControllerAddress == 0 || localPawnAddress == 0)
	{
		if (g_globalVars)
			g_globalVars->UpdateGlobalvars();
		InvalidateEntityState();
		return;
	}

	m_currentTick = 0;
	memoryManager.ReadMemory(
		localControllerAddress + Offset.PlayerController.m_nTickBase,
		m_currentTick);
	if (m_currentTick == 0)
	{
		InvalidateEntityState();
		return;
	}
	if (entitySnapshot.valid && m_currentTick < entitySnapshot.tick)
	{
		InvalidateEntityState();
		return;
	}

	const EntityListCache& currentCache = gGame.GetEntityListCache();
	const bool cacheInvalid =
		currentCache.root == 0 || !currentCache.validPages.test(0);
	const bool foregroundRequested = !backgroundRadarOnly;
	const bool refreshSnapshot =
		!entitySnapshot.valid ||
		cacheInvalid ||
		entitySnapshot.tick != m_currentTick ||
		entitySnapshot.localControllerAddress != localControllerAddress ||
		entitySnapshot.localPawnAddress != localPawnAddress ||
		entitySnapshot.entityListGeneration != currentCache.generation ||
		(foregroundRequested && !entitySnapshot.foregroundReady);

	if (refreshSnapshot)
	{
		if (!gGame.UpdateEntityListCache())
		{
			InvalidateEntityState();
			return;
		}

		const std::string mapName = GetCurrentMapName();
		if (mapName.empty())
		{
			InvalidateEntityState();
			return;
		}
		if (entitySnapshot.valid && entitySnapshot.mapName != mapName)
		{
			entitySnapshot = {};
			featureEntityCache.clear();
			previousFeatureTick = 0;
			WebRadar::Invalidate();
		}

		const EntityListCache& refreshedCache = gGame.GetEntityListCache();
		bool hydrationSucceeded = false;
		CEntity localEntity;
		const bool clientDataReady = localEntity.UpdateClientData();
		if (!clientDataReady)
		{
			AimControl::ResetRuntime();
			RCS::ResetRuntime();
		}

		if (localEntity.UpdateController(localControllerAddress))
		{
			const bool localPawnReady = localEntity.UpdatePawn(localPawnAddress);
			const bool localRadarPawnReady =
				localPawnReady || localEntity.UpdateRadarPawn(localPawnAddress);
			if (localRadarPawnReady)
			{
				int localPlayerControllerIndex = -1;
				std::vector<EntityBatchData> batchData;
				if (CollectEntityAddresses(
					localControllerAddress,
					localPlayerControllerIndex,
					batchData))
				{
					std::vector<std::pair<int, CEntity>> entities;
					EntityBatchProcessor processor;
					const EntityBatchStatus radarStatus =
						processor.ProcessRadarEntities(entities, batchData);
					if (radarStatus != EntityBatchStatus::Failed)
					{
						WebRadar::Publish(localEntity, entities, m_currentTick, mapName);

						EntitySnapshot candidate;
						candidate.tick = m_currentTick;
						candidate.entityListGeneration = refreshedCache.generation;
						candidate.mapName = mapName;
						candidate.localControllerAddress = localControllerAddress;
						candidate.localPawnAddress = localPawnAddress;
						candidate.localEntity = localEntity;
						candidate.localPlayerControllerIndex = localPlayerControllerIndex;
						candidate.entities = std::move(entities);
						candidate.valid = true;

						if (!backgroundRadarOnly && matrixReady && clientDataReady &&
							(localPawnReady || MenuConfig::WorkInSpec))
						{
							const EntityBatchStatus foregroundStatus =
								processor.ProcessForegroundEntities(
									candidate.entities,
									ESPConfig::ShowWeaponESP);
							if (foregroundStatus != EntityBatchStatus::Failed)
							{
								candidate.foregroundReady = true;
								featureEntityCache = candidate.entities;
							}
							else
							{
								featureEntityCache.clear();
							}
						}
						else
						{
							featureEntityCache.clear();
						}

						entitySnapshot = std::move(candidate);
						hydrationSucceeded = true;
					}
				}
			}
		}

		if (!hydrationSucceeded)
		{
			const bool canReuse =
				entitySnapshot.valid &&
				entitySnapshot.mapName == mapName &&
				entitySnapshot.localControllerAddress == localControllerAddress &&
				entitySnapshot.localPawnAddress == localPawnAddress &&
				entitySnapshot.entityListGeneration == refreshedCache.generation &&
				m_currentTick >= entitySnapshot.tick &&
				m_currentTick - entitySnapshot.tick <= 1;
			if (!canReuse)
			{
				InvalidateEntityState();
				return;
			}
			WebRadar::Publish(
				entitySnapshot.localEntity,
				entitySnapshot.entities,
				entitySnapshot.tick,
				entitySnapshot.mapName);
		}
	}

	if (!entitySnapshot.valid)
		return;

	CEntity& localEntity = entitySnapshot.localEntity;
	Base_Radar gameRadar;
	if (!backgroundRadarOnly && RadarCFG::ShowRadar &&
		(localEntity.Controller.TeamID != 0 || MenuConfig::ShowMenu))
	{
		RadarSetting(gameRadar);
	}

	if (backgroundRadarOnly || !matrixReady || !entitySnapshot.foregroundReady)
	{
		if (backgroundRadarOnly)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return;
	}

	const std::vector<EntityResult> entityResults =
		ProcessEntities(entitySnapshot.entities, localEntity);
	std::vector<AimControl::AimCandidate> aimCandidates;
	aimCandidates.reserve(entityResults.size());
	HandleEnts(
		entityResults,
		localEntity,
		entitySnapshot.localPlayerControllerIndex,
		gameRadar,
		aimCandidates);

	Visual(localEntity);
	Radar(gameRadar, localEntity);
	const bool currentSnapshot = entitySnapshot.tick == m_currentTick;
	MiscFuncs(localEntity, allowGameInput && currentSnapshot);
	if (allowGameInput && currentSnapshot)
		AIM(localEntity, aimCandidates);
	else
		AimControl::ResetRuntime();

	const int currentFPS = static_cast<int>(ImGui::GetIO().Framerate);
	if (currentFPS > MenuConfig::RenderFPS)
	{
		const int frameWait = round(1000.0f / MenuConfig::RenderFPS);
		std::this_thread::sleep_for(std::chrono::milliseconds(frameWait));
	}

	if (currentSnapshot && m_currentTick != previousFeatureTick)
	{
		if (allowGameInput)
			Trigger(localEntity, entitySnapshot.localPlayerControllerIndex);
		if (MiscCFG::SpecList)
			SpecList::GetSpectatorList(featureEntityCache, localEntity);
		previousFeatureTick = m_currentTick;
	}
}

bool Cheats::CollectEntityAddresses(
	const DWORD64 localControllerAddress,
	int& localPlayerControllerIndex,
	std::vector<EntityBatchData>& batchData)
{
	localPlayerControllerIndex = -1;
	batchData.clear();
	const EntityListCache& cache = gGame.GetEntityListCache();
	if (!cache.validPages.test(0) || cache.pages[0] == 0)
		return false;

	std::array<MemoryReadRequest, 64> controllerRequests{};
	std::array<DWORD64, 64> controllerAddresses{};
	std::array<std::uint8_t, 64> controllerSucceeded{};
	for (size_t entityIndex = 0; entityIndex < controllerRequests.size(); ++entityIndex)
	{
		controllerRequests[entityIndex] = {
			cache.pages[0] + (entityIndex + 1) * 0x70,
			sizeof(DWORD64)
		};
	}

	const MemoryBatchReadResult controllerResult =
		memoryManager.BatchReadMemoryBestEffort(
			controllerRequests,
			std::as_writable_bytes(std::span{ controllerAddresses }),
			MemoryReadPolicy::BypassDataCache,
			controllerSucceeded);
	if (!controllerResult.completed)
		return false;

	std::vector<MemoryReadRequest> handleRequests;
	std::vector<size_t> controllerIndices;
	for (size_t entityIndex = 0; entityIndex < controllerAddresses.size(); ++entityIndex)
	{
		const DWORD64 controllerAddress = controllerAddresses[entityIndex];
		if (controllerSucceeded[entityIndex] == 0 || controllerAddress == 0)
			continue;
		if (controllerAddress == localControllerAddress)
		{
			localPlayerControllerIndex = static_cast<int>(entityIndex);
			continue;
		}
		handleRequests.emplace_back(
			controllerAddress + Offset.Entity.PlayerPawn,
			sizeof(DWORD));
		controllerIndices.push_back(entityIndex);
	}

	if (handleRequests.empty())
		return true;

	std::vector<DWORD> pawnHandles(handleRequests.size(), 0);
	std::vector<std::uint8_t> handleSucceeded(handleRequests.size(), 0);
	const MemoryBatchReadResult handleResult = memoryManager.BatchReadMemoryBestEffort(
		handleRequests,
		std::as_writable_bytes(std::span{ pawnHandles }),
		MemoryReadPolicy::BypassDataCache,
		handleSucceeded);
	if (!handleResult.completed)
		return false;
	for (size_t index = 0; index < pawnHandles.size(); ++index)
	{
		if (handleSucceeded[index] == 0)
			pawnHandles[index] = 0;
	}

	std::vector<DWORD64> pawnAddresses(pawnHandles.size(), 0);
	std::vector<std::uint8_t> pawnSucceeded(pawnHandles.size(), 0);
	const MemoryBatchReadResult resolveResult =
		gGame.ResolveEntityHandles(pawnHandles, pawnAddresses, pawnSucceeded);
	if (!resolveResult.completed)
		return false;

	batchData.reserve(pawnAddresses.size());
	for (size_t index = 0; index < pawnAddresses.size(); ++index)
	{
		if (pawnSucceeded[index] == 0 || pawnAddresses[index] == 0)
			continue;
		const size_t entityIndex = controllerIndices[index];
		batchData.emplace_back(
			static_cast<int>(entityIndex),
			controllerAddresses[entityIndex],
			pawnAddresses[index]);
	}
	return true;
}

std::vector<EntityResult> Cheats::ProcessEntities(
	const std::vector<std::pair<int, CEntity>>& entities,
	const CEntity& localEntity)
{
	std::vector<EntityResult> results;
	results.reserve(entities.size());
	for (const auto& [entityIndex, entity] : entities)
	{
		EntityResult result;
		result.entityIndex = entityIndex;
		result.entity = entity;
		if (result.entity.Pawn.BoneData.BonePosList.empty() || !result.entity.IsAlive())
			continue;
		if (MenuConfig::TeamCheck &&
			result.entity.Controller.TeamID == localEntity.Controller.TeamID)
		{
			continue;
		}

		result.isInScreen = result.entity.IsInScreen();
		result.distance = static_cast<int>(
			result.entity.Pawn.Pos.DistanceTo(localEntity.Pawn.Pos) / 100);
		if (ESPConfig::ESPenabled && result.isInScreen)
			result.espRect = ESP::GetBoxRect(result.entity, ESPConfig::BoxType);
		if (ESPConfig::ESPenabled && ESPConfig::EnemySound &&
			result.entity.Controller.Address != localEntity.Controller.Address)
		{
			SoundESP::ProcessSound(result.entity, localEntity);
		}
		result.isValid = true;
		results.push_back(std::move(result));
	}
	return results;
}

// render, collect aim data
void Cheats::HandleEnts(const std::vector<EntityResult>& entities, CEntity& localEntity,
	int localPlayerControllerIndex, Base_Radar& gameRadar,
	std::vector<AimControl::AimCandidate>& aimCandidates)
{
	// healthbar map (static)
	static std::map<DWORD64, Render::HealthBar> HealthBarMap;


	for (const auto& result : entities)
	{
		if (!result.isValid)
		{
			if (HealthBarMap.count(result.entity.Controller.Address))
				HealthBarMap.erase(result.entity.Controller.Address);
			continue;
		}

		const auto& entity = result.entity;
		const int entityIndex = result.entityIndex;

		// add entity to radar
		if (RadarCFG::ShowRadar && localEntity.Controller.TeamID != 0)
		{
			gameRadar.AddPoint(localEntity.Pawn.Pos, localEntity.Pawn.ViewAngle.y, 
				entity.Pawn.Pos, ImColor(237, 85, 106, 200), RadarCFG::RadarType, entity.Pawn.ViewAngle.y);
		}

		// Out-of-FOV arrow
		if (localEntity.IsAlive()) {
			ESP::RenderOutOfFOVArrow(localEntity, result.entity);
		}

		// Collect one safe, closest on-screen hitbox candidate per entity.
		bool isVisible = !LegitBotConfig::VisibleCheck;
		if (!isVisible)
		{
			const bool localIndexValid = localPlayerControllerIndex >= 0 && localPlayerControllerIndex < 64;
			const bool entityIndexValid = entityIndex >= 0 && entityIndex < 64;
			const bool entitySpottedByLocal = localIndexValid &&
				(entity.Pawn.bSpottedByMask & (DWORD64(1) << localPlayerControllerIndex)) != 0;
			const bool localSpottedByEntity = entityIndexValid &&
				(localEntity.Pawn.bSpottedByMask & (DWORD64(1) << entityIndex)) != 0;
			isVisible = entitySpottedByLocal || localSpottedByEntity;
		}

		const float windowWidth = Gui.Window.Size.x;
		const float windowHeight = Gui.Window.Size.y;
		if (isVisible && !AimControl::HitboxList.empty() &&
			std::isfinite(windowWidth) && std::isfinite(windowHeight) &&
			windowWidth > 0.f && windowHeight > 0.f)
		{
			const auto& bones = entity.Pawn.BoneData.BonePosList;
			const Vec2 screenCenter{ windowWidth * 0.5f, windowHeight * 0.5f };
			const BoneJointPos* bestBone = nullptr;
			int bestHitbox = -1;
			float bestDistance = std::numeric_limits<float>::infinity();

			for (std::size_t i = 0; i < AimControl::HitboxList.size(); ++i)
			{
				const int hitbox = AimControl::HitboxList[i];
				bool duplicate = false;
				for (std::size_t previous = 0; previous < i; ++previous)
				{
					if (AimControl::HitboxList[previous] == hitbox)
					{
						duplicate = true;
						break;
					}
				}

				if (duplicate || hitbox < 0 || static_cast<std::size_t>(hitbox) >= bones.size())
					continue;

				const auto& bone = bones[static_cast<std::size_t>(hitbox)];
				if (!std::isfinite(bone.Pos.x) || !std::isfinite(bone.Pos.y) || !std::isfinite(bone.Pos.z) ||
					!std::isfinite(bone.ScreenPos.x) || !std::isfinite(bone.ScreenPos.y) ||
					bone.ScreenPos.x < 0.f || bone.ScreenPos.x > windowWidth ||
					bone.ScreenPos.y < 0.f || bone.ScreenPos.y > windowHeight)
					continue;

				const float distance = bone.ScreenPos.DistanceTo(screenCenter);
				if (std::isfinite(distance) && distance < bestDistance)
				{
					bestBone = &bone;
					bestHitbox = hitbox;
					bestDistance = distance;
				}
			}

			if (bestBone != nullptr)
				aimCandidates.push_back({ bestBone->Pos, entity.Pawn.Address, bestHitbox });
		}

		// Pawn origin visibility only constrains ESP, not aimbot bone collection.
		if (!result.isInScreen)
			continue;

		// render esp
		if (ESPConfig::ESPenabled && (!ESPConfig::FlashCheck || localEntity.Pawn.FlashDuration < 0.1f))
		{
			const ImVec4& Rect = result.espRect;
			const int distance = result.distance;

			if (MenuConfig::RenderDistance == 0 || (distance <= MenuConfig::RenderDistance && MenuConfig::RenderDistance > 0))
			{
				ESP::RenderPlayerESP(localEntity, entity, Rect, localPlayerControllerIndex, entityIndex);
				Render::DrawDistance(localEntity, entity, Rect);

				// healthbar
				if(ESPConfig::ShowHealthBar || ESPConfig::ShowHealthNum)
				{
					ImVec2 HealthBarPos = { Rect.x - 6.f, Rect.y };
					ImVec2 HealthBarSize = { 4, Rect.w };
					Render::DrawHealthBar(entity.Controller.Address, 100, entity.Pawn.Health, HealthBarPos, HealthBarSize);
				}


				// ammo
				// When player is using knife or nade, Ammo = -1.
				if (ESPConfig::AmmoBar && entity.Pawn.Ammo != -1)
				{
					ImVec2 AmmoBarPos = { Rect.x, Rect.y + Rect.w + 2 };
					ImVec2 AmmoBarSize = { Rect.z, 4 };
					Render::DrawAmmoBar(entity.Controller.Address, entity.Pawn.Ammo + entity.Pawn.ShotsFired, 
						entity.Pawn.Ammo, AmmoBarPos, AmmoBarSize);
				}

				// armor
				// It is meaningless to render a empty bar
				if ((ESPConfig::ArmorBar || ESPConfig::ShowArmorNum) && entity.Pawn.Armor > 0)
				{
					ImVec2 ArmorBarPos;
					
					if (ESPConfig::ShowHealthBar)
						ArmorBarPos = { Rect.x - 10.f, Rect.y };
					else
						ArmorBarPos = { Rect.x - 6.f, Rect.y };
					
					ImVec2 ArmorBarSize = { 4.f, Rect.w };
					Render::DrawArmorBar(entity.Controller.Address, 100, entity.Pawn.Armor, entity.Controller.HasHelmet, ArmorBarPos, ArmorBarSize);
				}
			}
		}
	}
}

void Menu() 
{
	if (MenuConfig::ShowMenu)
		GUI::DrawGui();

	GUI::InitHitboxList();
}

void Visual(const CEntity& LocalEntity)
{
	// Fov circle
	if (LocalEntity.Controller.TeamID != 0 && !MenuConfig::ShowMenu)
		Render::DrawFovCircle(ImGui::GetBackgroundDrawList(), LocalEntity);

	// Fov line
	Render::DrawFov(LocalEntity, LegitBotConfig::FovLineSize, LegitBotConfig::FovLineColor, 1);

	// HeadShoot Line
	Render::HeadShootLine(LocalEntity, MiscCFG::HeadShootLineColor);

	RenderCrosshair(ImGui::GetBackgroundDrawList(), LocalEntity);
}

void Radar(Base_Radar Radar, const CEntity& LocalEntity)
{
	// Radar render
	if ((RadarCFG::ShowRadar && LocalEntity.Controller.TeamID != 0) || (RadarCFG::ShowRadar && MenuConfig::ShowMenu))
	{
		Radar.Render();

		MenuConfig::RadarWinPos = ImGui::GetWindowPos();
		ImGui::End();
	}
}

void Trigger(const CEntity& LocalEntity, const int& LocalPlayerControllerIndex)
{
	// TriggerBot
	if (LegitBotConfig::TriggerBot && (GetAsyncKeyState(TriggerBot::HotKey) || LegitBotConfig::TriggerAlways))
		TriggerBot::Run(LocalEntity, LocalPlayerControllerIndex);
}

void AIM(const CEntity& LocalEntity, const std::vector<AimControl::AimCandidate>& aimCandidates)
{
	const bool aimKeyDown = (GetAsyncKeyState(AimControl::HotKey) & 0x8000) != 0;
	bool tracking = false;

	if (LegitBotConfig::AimBot && !MenuConfig::ShowMenu && aimKeyDown)
		tracking = AimControl::AimBot(LocalEntity, LocalEntity.Pawn.CameraPos, aimCandidates);
	else
		AimControl::ResetRuntime();

	const bool fireDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	RCS::RecoilControl(LocalEntity, fireDown, tracking);
}

void MiscFuncs(CEntity& LocalEntity, const bool allowGameInput)
{
    SpecList::SpectatorWindowList(LocalEntity);
    bmb::RenderWindow(LocalEntity.Controller.TeamID);
    SoundESP::Render();
    Misc::HitManager(LocalEntity, PreviousTotalHits);
    Misc::Watermark(LocalEntity);

    if (!allowGameInput)
        return;

    Misc::BunnyHop(LocalEntity);
    Misc::FastStop();
    Misc::AntiAFKKickUpdate();
    if (MiscCFG::AutoKnife && !MenuConfig::ShowMenu) {
        std::vector<CEntity> enemyList;
        enemyList.reserve(featureEntityCache.size());
        for (const auto& r : featureEntityCache) enemyList.push_back(r.second);
        Misc::KnifeBot(LocalEntity, enemyList);
    }
    if (MiscCFG::AutoZeus && !MenuConfig::ShowMenu) {
        Misc::ZeusBot(LocalEntity);
    }
}

void RadarSetting(Base_Radar& Radar)
{
	// Radar window
	ImGui::SetNextWindowBgAlpha(RadarCFG::RadarBgAlpha);
	ImGui::Begin("Radar", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);
	ImGui::SetWindowSize({ RadarCFG::RadarRange * 2,RadarCFG::RadarRange * 2 });
	ImGui::SetWindowPos(MenuConfig::RadarWinPos, ImGuiCond_Once);

	if (MenuConfig::RadarWinChengePos)
	{
		ImGui::SetWindowPos("Radar", MenuConfig::RadarWinPos);
		MenuConfig::RadarWinChengePos = false;
	}

	if (!RadarCFG::customRadar)
	{
		RadarCFG::ShowRadarCrossLine = false;
		RadarCFG::Proportion = 2700.f;
		RadarCFG::RadarPointSizeProportion = 1.f;
		RadarCFG::RadarRange = 125.f;
		RadarCFG::RadarBgAlpha = 0.1f;
	}


	// Radar.SetPos({ Gui.Window.Size.x / 2,Gui.Window.Size.y / 2 });
	Radar.SetDrawList(ImGui::GetWindowDrawList());
	Radar.SetPos({ ImGui::GetWindowPos().x + RadarCFG::RadarRange, ImGui::GetWindowPos().y + RadarCFG::RadarRange });
	Radar.SetProportion(RadarCFG::Proportion);
	Radar.SetRange(RadarCFG::RadarRange);
	Radar.SetSize(RadarCFG::RadarRange * 2);
	Radar.SetCrossColor(RadarCFG::RadarCrossLineColor);

	Radar.ArcArrowSize *= RadarCFG::RadarPointSizeProportion;
	Radar.ArrowSize *= RadarCFG::RadarPointSizeProportion;
	Radar.CircleSize *= RadarCFG::RadarPointSizeProportion;

	Radar.ShowCrossLine = RadarCFG::ShowRadarCrossLine;
	Radar.Opened = true;
}

void RenderCrosshair(ImDrawList* drawList, const CEntity& LocalEntity)
{
	if (!MiscCFG::SniperCrosshair || LocalEntity.Controller.TeamID == 0 || MenuConfig::ShowMenu)
		return;

	const std::string& currentWeapon = LocalEntity.Pawn.WeaponName;
	if (!TriggerBot::CheckScopeWeapon(currentWeapon) || LocalEntity.Pawn.IsScoped)
		return;

	Render::DrawCrossHair(drawList, ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), MiscCFG::SniperCrosshairColor);
}

std::string Cheats::GetCurrentMapName() {
    if (!g_globalVars || !g_globalVars->g_cCurrentMapName) {
        return "";
    }

    char currentMap[256] = { 0 };
    if (!memoryManager.ReadMemory(reinterpret_cast<DWORD64>(g_globalVars->g_cCurrentMapName),
        currentMap, sizeof(currentMap) - 1, MemoryReadPolicy::AllowDataCache)) {
        return "";
    }

    currentMap[255] = '\0';
    return std::string(currentMap);
}