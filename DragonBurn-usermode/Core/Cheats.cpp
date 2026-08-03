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
#include <future>
#include <iostream>
#include <cmath>
#include <limits>

#include "Cheats.h"
#include "Render.h"
#include "../Core/Config.h"

#include "../Core/Init.h"

#include "../Features/ESP.h"
#include "../Core/GUI.h"
#include "../Features/RCS.H"
#include "../Features/BombTimer.h"
#include "../Features/SpectatorList.h"
#include "../Helpers/Logger.h"
#include "../Features/SoundESP.h"
#include "../Features/WebRadar.h"

int PreviousTotalHits = 0;

void Menu();
void Visual(const CEntity&);
void Radar(Base_Radar, const CEntity&);
void Trigger(const CEntity&, const int&);
void AIM(const CEntity&, const std::vector<AimControl::AimCandidate>&);
void MiscFuncs(CEntity&);
void RenderCrosshair(ImDrawList*, const CEntity&);
void RadarSetting(Base_Radar&);

void Cheats::Run()
{	
	Menu();

	Misc::AutoAccept::UpdateAutoAccept();

	const HWND foregroundWindow = GetForegroundWindow();
	const bool backgroundRadarOnly = foregroundWindow != Init::Client::GetGameWindow()
		&& foregroundWindow != Gui.Window.hWnd;

	// Update matrix
	const bool matrixReady = memoryManager.ReadMemory(gGame.GetMatrixAddress(), gGame.View.Matrix, 64);

	// Update EntityList Entry
	gGame.UpdateEntityListEntry();

	DWORD64 LocalControllerAddress = 0;
	DWORD64 LocalPawnAddress = 0;

	if (!memoryManager.ReadMemory(gGame.GetLocalControllerAddress(), LocalControllerAddress))
	{
		WebRadar::Invalidate();
		return;
	}
	if (!memoryManager.ReadMemory(gGame.GetLocalPawnAddress(), LocalPawnAddress))
	{
		WebRadar::Invalidate();
		return;
	}

	if (LocalPawnAddress == 0 || LocalControllerAddress == 0) {
		g_globalVars->UpdateGlobalvars();
		cachedResults.clear();
		WebRadar::Invalidate();
		return;
	}

	// LocalEntity
	CEntity LocalEntity;
	int LocalPlayerControllerIndex = -1;
	const bool clientDataReady = LocalEntity.UpdateClientData();
	if (!clientDataReady)
	{
		AimControl::ResetRuntime();
		RCS::ResetRuntime();
	}
	if (!LocalEntity.UpdateController(LocalControllerAddress))
	{
		WebRadar::Invalidate();
		return;
	}
	const bool localPawnReady = LocalEntity.UpdatePawn(LocalPawnAddress);
	const bool localRadarPawnReady = localPawnReady || LocalEntity.UpdateRadarPawn(LocalPawnAddress);

	// Update m_currentTick
	bool success = memoryManager.ReadMemory<DWORD>(LocalEntity.Controller.Address + Offset.PlayerController.m_nTickBase, m_currentTick);
	if (!success) {
		m_currentTick = 0;
	}


	// radar data
	Base_Radar GameRadar;
	if (!backgroundRadarOnly && ((RadarCFG::ShowRadar && LocalEntity.Controller.TeamID != 0)
		|| (RadarCFG::ShowRadar && MenuConfig::ShowMenu)))
		RadarSetting(GameRadar);

	// process entities
	std::vector<EntityResult> entityResults;
	if (!backgroundRadarOnly && matrixReady && clientDataReady && (localPawnReady || MenuConfig::WorkInSpec))
		entityResults = ProcessEntities(LocalEntity, LocalPlayerControllerIndex);
	else
		CollectEntityData(LocalEntity, LocalPlayerControllerIndex);
	if (!localRadarPawnReady)
	{
		WebRadar::Invalidate();
	}
	else if (cachedResults.empty())
	{
		WebRadar::Invalidate();
	}
	else
	{
		WebRadar::Publish(LocalEntity, cachedResults, m_currentTick, GetCurrentMapName());
	}
	if (backgroundRadarOnly || !matrixReady || !clientDataReady || (!localPawnReady && !MenuConfig::WorkInSpec))
	{
		if (backgroundRadarOnly)
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		return;
	}
	std::vector<AimControl::AimCandidate> aimCandidates;
	aimCandidates.reserve(entityResults.size());
	
	// render, collect aim data
	HandleEnts(entityResults, LocalEntity, LocalPlayerControllerIndex, GameRadar, aimCandidates);

	Visual(LocalEntity);
	Radar(GameRadar, LocalEntity);
	MiscFuncs(LocalEntity);
	AIM(LocalEntity, aimCandidates);

	int currentFPS = static_cast<int>(ImGui::GetIO().Framerate);
	if (currentFPS > MenuConfig::RenderFPS)
	{
		int FrameWait = round(1000.0f / MenuConfig::RenderFPS);
		std::this_thread::sleep_for(std::chrono::milliseconds(FrameWait));
	}
	
	// Run trigger and spectator updates once per game tick.
	if (m_currentTick != m_previousTick)
	{
		Trigger(LocalEntity, LocalPlayerControllerIndex);
		
		std::vector<CEntity> allEntities;
		for (const auto& pair : cachedResults) {
			allEntities.push_back(pair.second);
		}
		SpecList::GetSpectatorList(allEntities, LocalEntity);
		m_previousTick = m_currentTick;
	}
}

// collect entity data
std::vector<std::pair<int, CEntity>> Cheats::CollectEntityData(CEntity& localEntity, int& localPlayerControllerIndex)
{
	// update only on new tick
	//if (m_currentTick == m_previousTick)
	//{
	//	return cachedResults;
	//}

	std::vector<EntityBatchData> batchData;
	batchData.reserve(64);

	// collect entity addresses
	for (int entityIndex = 0; entityIndex < 64; ++entityIndex)
	{
		DWORD64 entityAddress = 0;
		if (!memoryManager.ReadMemory<DWORD64>(gGame.GetEntityListEntry() + (entityIndex + 1) * 0x70, entityAddress))
		{
			continue;
		}

		// skip local player
		if (entityAddress == localEntity.Controller.Address)
		{
			localPlayerControllerIndex = entityIndex;
			continue;
		}

		// get pawn address
		CEntity tempEntity;
		tempEntity.Controller.Address = entityAddress;
		DWORD64 pawnAddress = tempEntity.Controller.GetPlayerPawnAddress();
		
		if (pawnAddress != 0)
		{
			batchData.emplace_back(entityIndex, entityAddress, pawnAddress);
		}
	}

	if (batchData.empty())
	{
		cachedResults.clear();
		WebRadar::Invalidate();
		return {};
	}

	// process all entities in batch
	std::vector<std::pair<int, CEntity>> entities;
	EntityBatchProcessor processor;
	if (!processor.ProcessAllEntities(entities, batchData))
	{
		cachedResults.clear();
		WebRadar::Invalidate();
		return {};
	}

	// update cache
	cachedResults = entities;

	return cachedResults;
}

// process, prepare results
std::vector<EntityResult> Cheats::ProcessEntities(CEntity& localEntity, int& localPlayerControllerIndex)
{
	// get batch-processed entities
	auto entities = CollectEntityData(localEntity, localPlayerControllerIndex);
	std::vector<EntityResult> results;
	results.reserve(entities.size());

	// process each entity
	for (auto& [entityIndex, entity] : entities)
	{
		EntityResult result;
		result.entityIndex = entityIndex;
		result.entity = entity;

		if (!entity.IsAlive())
			continue;

		// skip teammates if team check enabled
		if (MenuConfig::TeamCheck && entity.Controller.TeamID == localEntity.Controller.TeamID)
			continue;

		// check if in screen
		result.isInScreen = entity.IsInScreen();

		// calculate distance
		result.distance = static_cast<int>(entity.Pawn.Pos.DistanceTo(localEntity.Pawn.Pos) / 100);

		// calculate esp box rect
		if (ESPConfig::ESPenabled && result.isInScreen)
			result.espRect = ESP::GetBoxRect(entity, ESPConfig::BoxType);

		// sound esp
		if (ESPConfig::ESPenabled && ESPConfig::EnemySound && result.entity.Controller.Address != localEntity.Controller.Address)
			SoundESP::ProcessSound(result.entity, localEntity);

		result.isValid = true;
		results.push_back(result);
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
					bool HasHelmet;
					ImVec2 ArmorBarPos;
					memoryManager.ReadMemory(entity.Controller.Address + Offset.PlayerController.HasHelmet, HasHelmet);
					
					if (ESPConfig::ShowHealthBar)
						ArmorBarPos = { Rect.x - 10.f, Rect.y };
					else
						ArmorBarPos = { Rect.x - 6.f, Rect.y };
					
					ImVec2 ArmorBarSize = { 4.f, Rect.w };
					Render::DrawArmorBar(entity.Controller.Address, 100, entity.Pawn.Armor, HasHelmet, ArmorBarPos, ArmorBarSize);
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

void MiscFuncs(CEntity& LocalEntity)
{
    SpecList::SpectatorWindowList(LocalEntity);
    bmb::RenderWindow(LocalEntity.Controller.TeamID);
    SoundESP::Render();

    Misc::HitManager(LocalEntity, PreviousTotalHits);
    Misc::BunnyHop(LocalEntity);
    Misc::Watermark(LocalEntity);
    Misc::FastStop();
    Misc::AntiAFKKickUpdate();
    if (MiscCFG::AutoKnife && !MenuConfig::ShowMenu) {
        std::vector<CEntity> enemyList;
        enemyList.reserve(Cheats::cachedResults.size());
        for (const auto& r : Cheats::cachedResults) enemyList.push_back(r.second);
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

	bool isScoped;
	memoryManager.ReadMemory<bool>(LocalEntity.Pawn.Address + Offset.Pawn.isScoped, isScoped);
	std::string curWeapon = TriggerBot::GetWeapon(LocalEntity);

	if (!TriggerBot::CheckScopeWeapon(curWeapon) || isScoped)
		return;

	Render::DrawCrossHair(drawList, ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2), MiscCFG::SniperCrosshairColor);
}

std::string Cheats::GetCurrentMapName() {
    if (!g_globalVars || !g_globalVars->g_cCurrentMapName) {
        return "";
    }

    char currentMap[256] = { 0 };
    if (!memoryManager.ReadMemory(reinterpret_cast<DWORD64>(g_globalVars->g_cCurrentMapName),
        currentMap, sizeof(currentMap) - 1)) {
        return "";
    }

    currentMap[255] = '\0';
    return std::string(currentMap);
}