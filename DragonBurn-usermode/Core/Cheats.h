#pragma once
#include "../Game/Game.h"
#include "../Game/Entity.h"
#include "../Features\Aimbot.h"
#include "../Features/Radar.h"
#include "../Features/TriggerBot.h"
#include "../Features/Misc.h"
#include <vector>

// processed entity data and results
struct EntityResult {
	int entityIndex = -1;
	CEntity entity;
	ImVec4 espRect;
	int distance = 0;
	bool isInScreen = false;
	bool isValid = false;
};

namespace Cheats
{

	void Run();
	
	bool CollectEntityAddresses(
		DWORD64 localControllerAddress,
		int& localPlayerControllerIndex,
		std::vector<EntityBatchData>& batchData);
	std::vector<EntityResult> ProcessEntities(
		const std::vector<std::pair<int, CEntity>>& entities,
		const CEntity& localEntity);
	void HandleEnts(const std::vector<EntityResult>& entities, CEntity& localEntity,
		int localPlayerControllerIndex, Base_Radar& gameRadar,
		std::vector<AimControl::AimCandidate>& aimCandidates);

	std::string GetCurrentMapName();
}

struct {
	ImFont* normal15px = nullptr;
} fonts;