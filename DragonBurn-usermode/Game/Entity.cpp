#include "Entity.h"
#include <algorithm>
#include <cstring>
#undef min
#undef max


std::unordered_map<int, std::string> CEntity::weaponNames = {
	{1, "deagle"},
	{2, "elite"},
	{3, "fiveseven"},
	{4, "glock"},
	{7, "ak47"},
	{8, "aug"},
	{9, "awp"},
	{10, "famas"},
	{11, "g3Sg1"},
	{13, "galilar"},
	{14, "m249"},
	{16, "m4a4"},
	{17, "mac10"},
	{19, "p90"},
	{23, "mp5sd"},
	{24, "ump45"},
	{25, "xm1014"},
	{26, "bizon"},
	{27, "mag7"},
	{28, "negev"},
	{29, "sawedoff"},
	{30, "tec9"},
	{31, "zeus"},
	{32, "p2000"},
	{33, "mp7"},
	{34, "mp9"},
	{35, "nova"},
	{36, "p250"},
	{38, "scar20"},
	{39, "sg556"},
	{40, "ssg08"},
	{42, "ct_knife"},
	{43, "flashbang"},
	{44, "hegrenade"},
	{45, "smokegrenade"},
	{46, "molotov"},
	{47, "decoy"},
	{48, "incgrenade"},
	{49, "c4"},
	{59, "t_knife"},
	{60, "m4a1"},
	{61, "usp"},
	{63, "cz75a"},
	{64, "revolver"}
};

bool CEntity::UpdateController(const DWORD64& PlayerControllerAddress)
{
	if (PlayerControllerAddress == 0)
		return false;
	this->Controller.Address = PlayerControllerAddress;

	if (!this->Controller.GetHealth())
		return false;
	if (!this->Controller.GetIsAlive())
		return false;
	if (!this->Controller.GetTeamID())
		return false;
	if (!this->Controller.GetPlayerName())
		return false;
	if (!this->Controller.GetPlayerSteamID())
		return false;


	return true;
}

bool CEntity::UpdatePawn(const DWORD64& PlayerPawnAddress)
{
	if (PlayerPawnAddress == 0)
		return false;
	this->Pawn.Address = PlayerPawnAddress;
	this->Pawn.IsScoped = false;
	this->Pawn.GetIsScoped();

	if (!this->Pawn.GetCameraPos())//
		return false;
	if (!this->Pawn.GetPos())//
		return false;
	if (!this->Pawn.GetViewAngle())//
		return false;
	if (!this->Pawn.GetWeaponName())//
		return false;
	if (!this->Pawn.GetAimPunchAngle())//
		return false;
	if (!this->Pawn.GetShotsFired())//
		return false;
	if (!this->Pawn.GetHealth())//
		return false;
	if (!this->Pawn.GetLifeState())//
		return false;
	if (!this->Pawn.GetAmmo())//
		return false;
	//if (!this->Pawn.GetMaxAmmo())
	//	return false;
	if (!this->Pawn.GetArmor())//
		return false;
	if (!this->Pawn.GetTeamID())//
		return false;
	if (!this->Pawn.GetFov())
		return false;
	if (!this->Pawn.GetSpotted())//
		return false;
	if (!this->Pawn.GetFFlags())
		return false;
	//if (!this->Pawn.GetDefusing())
	//	return false;
	if (!this->Pawn.GetFlashDuration())//
		return false;
	if (!this->Pawn.GetVelocity())
		return false;

	return true;
}

bool CEntity::UpdateRadarPawn(const DWORD64& PlayerPawnAddress)
{
	if (PlayerPawnAddress == 0)
		return false;
	this->Pawn.Address = PlayerPawnAddress;

	if (!this->Pawn.GetPos())
		return false;
	if (!this->Pawn.GetViewAngle())
		return false;
	if (!this->Pawn.GetHealth())
		return false;
	if (!this->Pawn.GetLifeState())
		return false;
	if (!this->Pawn.GetWeaponName())
		this->Pawn.WeaponName.clear();

	return true;
}

bool CEntity::UpdateClientData()
{
	if (!this->Client.GetSensitivity())
		return false;

	return true;
}

bool PlayerController::GetTeamID()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.iTeamNum, this->TeamID);
}

bool PlayerController::GetHealth()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.CurrentHealth, this->Health);
}

bool PlayerController::GetIsAlive()
{
	return GetDataAddressWithOffset<bool>(Address, Offset.Entity.IsAlive, this->AliveStatus);
}

bool PlayerController::GetPlayerName()
{
	char Buffer[MAX_PATH]{};

	if (!memoryManager.ReadMemory(Address + Offset.Entity.iszPlayerName, Buffer, MAX_PATH))
		return false;

	//if (!this->SteamID)
	//	this->PlayerName = "BOT " + std::string(Buffer);
	//else
	this->PlayerName = Buffer;

	if (this->PlayerName.empty())
		this->PlayerName = "Name_None";

	return true;
}

bool PlayerController::GetPlayerSteamID()
{
	return GetDataAddressWithOffset<INT64>(Address, Offset.PlayerController.m_steamID, this->SteamID);
}

bool PlayerPawn::GetViewAngle()
{
	return GetDataAddressWithOffset<Vec2>(Address, Offset.Pawn.angEyeAngles, this->ViewAngle);
}

bool PlayerPawn::GetCameraPos()
{
	return GetDataAddressWithOffset<Vec3>(Address, Offset.Pawn.LastCameraSetupLocalOrigin, this->CameraPos);
}

bool PlayerPawn::GetSpotted()
{
	return GetDataAddressWithOffset<DWORD64>(Address, Offset.Pawn.bSpottedByMask, this->bSpottedByMask);
}

bool PlayerPawn::GetFFlags()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.fFlags, this->fFlags);
}

DWORD64 PlayerPawn::GetActiveWeaponAddress() const
{
	DWORD64 weaponServices = 0;
	if (!memoryManager.ReadMemory(Address + Offset.Pawn.m_pWeaponServices, weaponServices) || weaponServices == 0)
		return 0;

	DWORD activeWeaponHandle = 0;
	if (!memoryManager.ReadMemory(weaponServices + Offset.WeaponBaseData.hActiveWeapon, activeWeaponHandle) ||
		activeWeaponHandle == 0 || activeWeaponHandle == 0xFFFFFFFF)
		return 0;

	return gGame.ResolveEntityHandle(activeWeaponHandle);
}

bool PlayerPawn::GetWeaponName()
{
	const DWORD64 CurrentWeapon = GetActiveWeaponAddress();
	if (CurrentWeapon == 0)
		return false;

	// Calculate the final address for weapon index directly
	DWORD64 weaponIndexAddress = CurrentWeapon + Offset.EconEntity.AttributeManager +
		Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex;

	// Single memory read to get weapon index
	short weaponIndex;
	if (!memoryManager.ReadMemory(weaponIndexAddress, weaponIndex) || weaponIndex == -1)
		return false;

	// Inline weapon name lookup
	static const std::string defaultWeapon = "Weapon_None";
	auto it = CEntity::weaponNames.find(weaponIndex);
	WeaponName = (it != CEntity::weaponNames.end()) ? it->second : defaultWeapon;

	return true;
}

bool PlayerPawn::GetIsScoped()
{
	return GetDataAddressWithOffset<bool>(Address, Offset.Pawn.isScoped, this->IsScoped);
}

bool PlayerPawn::GetShotsFired()
{
	return GetDataAddressWithOffset<DWORD>(Address, Offset.Pawn.iShotsFired, this->ShotsFired);
}

bool PlayerPawn::GetAimPunchAngle()
{
	DWORD64 aimPunchServices = 0;
	if (!memoryManager.ReadMemory(Address + Offset.Pawn.AimPunchServices, aimPunchServices) || aimPunchServices == 0)
		return false;

	return GetDataAddressWithOffset<Vec2>(aimPunchServices, Offset.Pawn.PredictableAimPunchAngle, this->AimPunchAngle);
}

bool PlayerPawn::GetTeamID()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.iTeamNum, this->TeamID);
}



bool PlayerPawn::GetPos()
{
	return GetDataAddressWithOffset<Vec3>(Address, Offset.Pawn.Pos, this->Pos);
}

bool PlayerPawn::GetHealth()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.CurrentHealth, this->Health);
}

bool PlayerPawn::GetLifeState()
{
	return GetDataAddressWithOffset<BYTE>(Address, Offset.Pawn.LifeState, this->LifeState);
}

bool PlayerPawn::GetArmor()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.CurrentArmor, this->Armor);
}

bool PlayerPawn::GetAmmo()
{
	const DWORD64 CurrentWeapon = GetActiveWeaponAddress();
	if (CurrentWeapon == 0)
		return false;

	return GetDataAddressWithOffset<int>(CurrentWeapon, Offset.WeaponBaseData.Clip1, this->Ammo);
}

bool PlayerPawn::GetFov()
{
	DWORD64 CameraServices = 0;
	if (!memoryManager.ReadMemory<DWORD64>(Address + Offset.Pawn.CameraServices, CameraServices))
		return false;
	return GetDataAddressWithOffset<int>(CameraServices, Offset.Pawn.iFovStart, this->Fov);
}

//bool PlayerPawn::GetDefusing()
//{
//	return memoryManager.ReadMemory(Address + Offset.C4.m_bBeingDefused, this->isDefusing);
//}

bool PlayerPawn::GetFlashDuration()
{
	return memoryManager.ReadMemory(Address + Offset.Pawn.flFlashDuration, this->FlashDuration);
}

bool PlayerPawn::GetVelocity()
{
	Vec3 Velocity;
	if (!memoryManager.ReadMemory(Address + Offset.Pawn.AbsVelocity, Velocity))
		return false;
	this->Speed = sqrt(Velocity.x * Velocity.x + Velocity.y * Velocity.y);
	return true;
}


bool CEntity::IsAlive() const
{
	return this->Controller.AliveStatus && this->Pawn.LifeState == 0;
}

bool CEntity::IsInScreen()
{
    if (!gGame.View.WorldToScreen(this->Pawn.Pos, this->Pawn.ScreenPos))
        return false;
    
    return (this->Pawn.ScreenPos.x >= 0 && this->Pawn.ScreenPos.x <= Gui.Window.Size.x &&
            this->Pawn.ScreenPos.y >= 0 && this->Pawn.ScreenPos.y <= Gui.Window.Size.y);
}

CBone CEntity::GetBone() const
{
	if (this->Pawn.Address == 0)
		return CBone{};
	return this->Pawn.BoneData;
}


bool Client::GetSensitivity()
{
	DWORD64 ptr = 0;
	if (!memoryManager.ReadMemory(gGame.GetClientDLLAddress() + Offset.Sensitivity, ptr))
		return false;

	if (ptr == 0)
		return false;

	float flSensitivity = 0.0f;
	if (!memoryManager.ReadMemory(ptr + Offset.Sensitivity_sensitivity, flSensitivity))
		return false;

	this->Sensitivity = flSensitivity;
	return true;
}

namespace
{
	EntityBatchStatus ReduceBatchStatus(const size_t survivors, const size_t requested)
	{
		if (survivors == 0)
			return EntityBatchStatus::Empty;
		return survivors < requested ? EntityBatchStatus::Partial : EntityBatchStatus::Ready;
	}
}

EntityBatchStatus EntityBatchProcessor::ProcessRadarEntities(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::span<const EntityBatchData> batchData)
{
	radarInitialized_ = false;
	radarInputCount_ = batchData.size();
	entities.clear();
	weaponServiceAddresses_.clear();
	aimPunchServiceAddresses_.clear();
	cameraAddresses_.clear();
	weaponAddresses_.clear();

	entities.reserve(batchData.size());
	for (const EntityBatchData& data : batchData)
	{
		CEntity entity;
		entity.Controller.Address = data.controllerAddress;
		entity.Pawn.Address = data.pawnAddress;
		entities.emplace_back(data.entityIndex, std::move(entity));
	}

	if (batchData.empty())
	{
		radarInitialized_ = true;
		return EntityBatchStatus::Empty;
	}

	if (!ProcessCoreEntityData(entities) ||
		!ProcessServiceData(entities) ||
		!ProcessRadarWeaponNames(entities))
	{
		entities.clear();
		weaponServiceAddresses_.clear();
		aimPunchServiceAddresses_.clear();
		cameraAddresses_.clear();
		weaponAddresses_.clear();
		return EntityBatchStatus::Failed;
	}

	radarInitialized_ = true;
	return ReduceBatchStatus(entities.size(), radarInputCount_);
}

EntityBatchStatus EntityBatchProcessor::ProcessForegroundEntities(
	std::vector<std::pair<int, CEntity>>& entities,
	const bool includeInventory)
{
	if (!radarInitialized_ ||
		entities.size() != weaponServiceAddresses_.size() ||
		entities.size() != aimPunchServiceAddresses_.size() ||
		entities.size() != cameraAddresses_.size() ||
		entities.size() != weaponAddresses_.size())
	{
		return EntityBatchStatus::Failed;
	}

	if (entities.empty())
		return EntityBatchStatus::Empty;

	if (!ProcessWeaponData(entities) || !ProcessDependenciesData(entities))
		return EntityBatchStatus::Failed;

	if (includeInventory)
		ProcessInventoryData(entities);

	if (!ProcessBoneData(entities))
		return EntityBatchStatus::Failed;

	return ReduceBatchStatus(entities.size(), radarInputCount_);
}

bool EntityBatchProcessor::ProcessCoreEntityData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	constexpr size_t RequestsPerEntity = 23;
	std::vector<MemoryReadRequest> requests;
	requests.reserve(entities.size() * RequestsPerEntity);
	for (const auto& [entityIndex, entity] : entities)
	{
		requests.emplace_back(entity.Controller.Address + Offset.Pawn.CurrentHealth, sizeof(int));
		requests.emplace_back(entity.Controller.Address + Offset.Entity.IsAlive, sizeof(bool));
		requests.emplace_back(entity.Controller.Address + Offset.Pawn.iTeamNum, sizeof(int));
		requests.emplace_back(entity.Controller.Address + Offset.Entity.iszPlayerName, MAX_PATH);
		requests.emplace_back(entity.Controller.Address + Offset.PlayerController.m_steamID, sizeof(INT64));
		requests.emplace_back(entity.Controller.Address + Offset.PlayerController.HasHelmet, sizeof(bool));

		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.angEyeAngles, sizeof(Vec2));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.LastCameraSetupLocalOrigin, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.Pos, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.bSpottedByMask, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.fFlags, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.iShotsFired, sizeof(DWORD));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.iTeamNum, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CurrentHealth, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.LifeState, sizeof(BYTE));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CurrentArmor, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.flFlashDuration, sizeof(float));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.AbsVelocity, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.m_pWeaponServices, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CameraServices, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.AimPunchServices, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.isScoped, sizeof(bool));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.m_flEmitSoundTime, sizeof(float));
	}

	SIZE_T totalSize = 0;
	for (const MemoryReadRequest& request : requests)
		totalSize += request.size;

	std::vector<BYTE> buffer(totalSize, 0);
	std::vector<std::uint8_t> requestSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ buffer }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed)
		return false;

	weaponServiceAddresses_.assign(entities.size(), 0);
	aimPunchServiceAddresses_.assign(entities.size(), 0);
	cameraAddresses_.assign(entities.size(), 0);
	std::vector<bool> keep(entities.size(), false);
	SIZE_T bufferOffset = 0;
	size_t requestIndex = 0;
	for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
	{
		CEntity& entity = entities[entityIndex].second;
		const size_t firstRequest = requestIndex;
		auto readValue = [&](void* destination, const SIZE_T size)
		{
			if (requestSucceeded[requestIndex] != 0)
				std::memcpy(destination, buffer.data() + bufferOffset, size);
			bufferOffset += size;
			++requestIndex;
		};

		entity.Controller.Health = 0;
		entity.Controller.AliveStatus = false;
		entity.Controller.TeamID = 0;
		entity.Controller.PlayerName = "Name_None";
		entity.Controller.SteamID = 0;
		entity.Controller.HasHelmet = false;
		readValue(&entity.Controller.Health, sizeof(int));
		readValue(&entity.Controller.AliveStatus, sizeof(bool));
		readValue(&entity.Controller.TeamID, sizeof(int));
		if (requestSucceeded[requestIndex] != 0)
		{
			const char* name = reinterpret_cast<const char*>(buffer.data() + bufferOffset);
			entity.Controller.PlayerName.assign(name, strnlen_s(name, MAX_PATH));
			if (entity.Controller.PlayerName.empty())
				entity.Controller.PlayerName = "Name_None";
		}
		bufferOffset += MAX_PATH;
		++requestIndex;
		readValue(&entity.Controller.SteamID, sizeof(INT64));
		readValue(&entity.Controller.HasHelmet, sizeof(bool));

		entity.Pawn.ViewAngle = Vec2();
		entity.Pawn.CameraPos = {};
		entity.Pawn.Pos = {};
		entity.Pawn.bSpottedByMask = 0;
		entity.Pawn.fFlags = 0;
		entity.Pawn.ShotsFired = 0;
		entity.Pawn.TeamID = 0;
		entity.Pawn.Health = 0;
		entity.Pawn.LifeState = 0xFF;
		entity.Pawn.Armor = 0;
		entity.Pawn.FlashDuration = 0.0f;
		entity.Pawn.Speed = 0.0f;
		entity.Pawn.IsScoped = false;
		entity.Pawn.EmitSoundTime = 0.0f;
		entity.Pawn.EmitSoundTimeValid = false;
		readValue(&entity.Pawn.ViewAngle, sizeof(Vec2));
		readValue(&entity.Pawn.CameraPos, sizeof(Vec3));
		readValue(&entity.Pawn.Pos, sizeof(Vec3));
		readValue(&entity.Pawn.bSpottedByMask, sizeof(DWORD64));
		readValue(&entity.Pawn.fFlags, sizeof(int));
		readValue(&entity.Pawn.ShotsFired, sizeof(DWORD));
		readValue(&entity.Pawn.TeamID, sizeof(int));
		readValue(&entity.Pawn.Health, sizeof(int));
		readValue(&entity.Pawn.LifeState, sizeof(BYTE));
		readValue(&entity.Pawn.Armor, sizeof(int));
		readValue(&entity.Pawn.FlashDuration, sizeof(float));
		Vec3 velocity{};
		const bool velocityReady = requestSucceeded[requestIndex] != 0;
		readValue(&velocity, sizeof(Vec3));
		if (velocityReady)
			entity.Pawn.Speed = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		readValue(&weaponServiceAddresses_[entityIndex], sizeof(DWORD64));
		readValue(&cameraAddresses_[entityIndex], sizeof(DWORD64));
		readValue(&aimPunchServiceAddresses_[entityIndex], sizeof(DWORD64));
		readValue(&entity.Pawn.IsScoped, sizeof(bool));
		const bool soundReady = requestSucceeded[requestIndex] != 0;
		readValue(&entity.Pawn.EmitSoundTime, sizeof(float));
		entity.Pawn.EmitSoundTimeValid = soundReady;

		keep[entityIndex] =
			requestSucceeded[firstRequest + 1] != 0 &&
			requestSucceeded[firstRequest + 2] != 0 &&
			requestSucceeded[firstRequest + 3] != 0 &&
			requestSucceeded[firstRequest + 6] != 0 &&
			requestSucceeded[firstRequest + 8] != 0 &&
			requestSucceeded[firstRequest + 13] != 0 &&
			requestSucceeded[firstRequest + 14] != 0;
	}

	std::vector<std::pair<int, CEntity>> keptEntities;
	std::vector<DWORD64> keptWeaponServices;
	std::vector<DWORD64> keptAimPunchServices;
	std::vector<DWORD64> keptCameras;
	keptEntities.reserve(entities.size());
	keptWeaponServices.reserve(entities.size());
	keptAimPunchServices.reserve(entities.size());
	keptCameras.reserve(entities.size());
	for (size_t index = 0; index < entities.size(); ++index)
	{
		if (!keep[index])
			continue;
		keptEntities.push_back(std::move(entities[index]));
		keptWeaponServices.push_back(weaponServiceAddresses_[index]);
		keptAimPunchServices.push_back(aimPunchServiceAddresses_[index]);
		keptCameras.push_back(cameraAddresses_[index]);
	}

	entities = std::move(keptEntities);
	weaponServiceAddresses_ = std::move(keptWeaponServices);
	aimPunchServiceAddresses_ = std::move(keptAimPunchServices);
	cameraAddresses_ = std::move(keptCameras);
	return true;
}

bool EntityBatchProcessor::ProcessServiceData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	struct RequestMap
	{
		size_t entityIndex;
		bool activeWeapon;
	};

	std::vector<MemoryReadRequest> requests;
	std::vector<RequestMap> requestMap;
	requests.reserve(entities.size() * 2);
	requestMap.reserve(entities.size() * 2);
	std::vector<DWORD> activeWeaponHandles(entities.size(), 0);
	for (size_t index = 0; index < entities.size(); ++index)
	{
		entities[index].second.Pawn.AimPunchAngle = Vec2();
		if (weaponServiceAddresses_[index] != 0)
		{
			requests.emplace_back(
				weaponServiceAddresses_[index] + Offset.WeaponBaseData.hActiveWeapon,
				sizeof(DWORD));
			requestMap.push_back({ index, true });
		}
		if (aimPunchServiceAddresses_[index] != 0)
		{
			requests.emplace_back(
				aimPunchServiceAddresses_[index] + Offset.Pawn.PredictableAimPunchAngle,
				sizeof(Vec2));
			requestMap.push_back({ index, false });
		}
	}

	weaponAddresses_.assign(entities.size(), 0);
	if (requests.empty())
		return true;

	SIZE_T totalSize = 0;
	for (const MemoryReadRequest& request : requests)
		totalSize += request.size;
	std::vector<BYTE> buffer(totalSize, 0);
	std::vector<std::uint8_t> requestSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ buffer }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed)
		return false;

	SIZE_T bufferOffset = 0;
	for (size_t requestIndex = 0; requestIndex < requestMap.size(); ++requestIndex)
	{
		const RequestMap& mapping = requestMap[requestIndex];
		if (mapping.activeWeapon)
		{
			if (requestSucceeded[requestIndex] != 0)
				std::memcpy(&activeWeaponHandles[mapping.entityIndex], buffer.data() + bufferOffset, sizeof(DWORD));
			bufferOffset += sizeof(DWORD);
		}
		else
		{
			if (requestSucceeded[requestIndex] != 0)
			{
				std::memcpy(
					&entities[mapping.entityIndex].second.Pawn.AimPunchAngle,
					buffer.data() + bufferOffset,
					sizeof(Vec2));
			}
			bufferOffset += sizeof(Vec2);
		}
	}

	std::vector<std::uint8_t> resolvedSucceeded(entities.size(), 0);
	const MemoryBatchReadResult resolveResult = gGame.ResolveEntityHandles(
		activeWeaponHandles,
		weaponAddresses_,
		resolvedSucceeded);
	return resolveResult.completed;
}

bool EntityBatchProcessor::ProcessRadarWeaponNames(
	std::vector<std::pair<int, CEntity>>& entities)
{
	std::vector<MemoryReadRequest> requests;
	std::vector<size_t> entityIndices;
	for (size_t index = 0; index < weaponAddresses_.size(); ++index)
	{
		entities[index].second.Pawn.WeaponName.clear();
		if (weaponAddresses_[index] == 0)
			continue;
		requests.emplace_back(
			weaponAddresses_[index] + Offset.EconEntity.AttributeManager +
				Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex,
			sizeof(short));
		entityIndices.push_back(index);
	}

	if (requests.empty())
		return true;

	std::vector<short> weaponIndices(requests.size(), -1);
	std::vector<std::uint8_t> requestSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ weaponIndices }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed)
		return false;

	for (size_t index = 0; index < entityIndices.size(); ++index)
	{
		if (requestSucceeded[index] != 0)
			entities[entityIndices[index]].second.Pawn.WeaponName =
				CEntity::GetWeaponName(weaponIndices[index]);
	}
	return true;
}

bool EntityBatchProcessor::ProcessWeaponData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	struct RequestMap
	{
		size_t entityIndex;
		bool ammo;
	};

	std::vector<MemoryReadRequest> requests;
	std::vector<RequestMap> requestMap;
	for (size_t index = 0; index < weaponAddresses_.size(); ++index)
	{
		entities[index].second.Pawn.Ammo = 0;
		if (weaponAddresses_[index] == 0)
			continue;
		requests.emplace_back(weaponAddresses_[index] + Offset.WeaponBaseData.Clip1, sizeof(int));
		requestMap.push_back({ index, true });
		requests.emplace_back(
			weaponAddresses_[index] + Offset.EconEntity.AttributeManager +
				Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex,
			sizeof(short));
		requestMap.push_back({ index, false });
	}

	if (requests.empty())
		return true;

	SIZE_T totalSize = 0;
	for (const MemoryReadRequest& request : requests)
		totalSize += request.size;
	std::vector<BYTE> buffer(totalSize, 0);
	std::vector<std::uint8_t> requestSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ buffer }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed)
		return false;

	SIZE_T bufferOffset = 0;
	for (size_t requestIndex = 0; requestIndex < requestMap.size(); ++requestIndex)
	{
		const RequestMap& mapping = requestMap[requestIndex];
		if (mapping.ammo)
		{
			if (requestSucceeded[requestIndex] != 0)
				std::memcpy(&entities[mapping.entityIndex].second.Pawn.Ammo, buffer.data() + bufferOffset, sizeof(int));
			bufferOffset += sizeof(int);
		}
		else
		{
			if (requestSucceeded[requestIndex] != 0)
			{
				short weaponIndex = -1;
				std::memcpy(&weaponIndex, buffer.data() + bufferOffset, sizeof(short));
				entities[mapping.entityIndex].second.Pawn.WeaponName = CEntity::GetWeaponName(weaponIndex);
			}
			bufferOffset += sizeof(short);
		}
	}
	return true;
}

bool EntityBatchProcessor::ProcessDependenciesData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	std::vector<MemoryReadRequest> requests;
	std::vector<size_t> entityIndices;
	for (size_t index = 0; index < cameraAddresses_.size(); ++index)
	{
		entities[index].second.Pawn.Fov = 90;
		if (cameraAddresses_[index] == 0)
			continue;
		requests.emplace_back(cameraAddresses_[index] + Offset.Pawn.iFovStart, sizeof(int));
		entityIndices.push_back(index);
	}

	if (requests.empty())
		return true;

	std::vector<int> fovValues(requests.size(), 90);
	std::vector<std::uint8_t> requestSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ fovValues }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed)
		return false;

	for (size_t index = 0; index < entityIndices.size(); ++index)
	{
		if (requestSucceeded[index] != 0)
			entities[entityIndices[index]].second.Pawn.Fov = fovValues[index];
	}
	return true;
}

void EntityBatchProcessor::ProcessInventoryData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	struct InventoryHeader
	{
		int count = 0;
		DWORD64 data = 0;
	};

	std::vector<MemoryReadRequest> headerRequests;
	std::vector<size_t> headerOwners;
	for (size_t index = 0; index < weaponServiceAddresses_.size(); ++index)
	{
		entities[index].second.Pawn.HasC4 = false;
		if (weaponServiceAddresses_[index] == 0)
			continue;
		headerRequests.emplace_back(
			weaponServiceAddresses_[index] + Offset.WeaponBaseData.hMyWeapons,
			sizeof(int));
		headerRequests.emplace_back(
			weaponServiceAddresses_[index] + Offset.WeaponBaseData.hMyWeapons + 0x8,
			sizeof(DWORD64));
		headerOwners.push_back(index);
	}
	if (headerRequests.empty())
		return;

	SIZE_T headerSize = 0;
	for (const MemoryReadRequest& request : headerRequests)
		headerSize += request.size;
	std::vector<BYTE> headerBuffer(headerSize, 0);
	std::vector<std::uint8_t> headerSucceeded(headerRequests.size(), 0);
	const MemoryBatchReadResult headerResult = memoryManager.BatchReadMemoryBestEffort(
		headerRequests,
		std::as_writable_bytes(std::span{ headerBuffer }),
		MemoryReadPolicy::BypassDataCache,
		headerSucceeded);
	if (!headerResult.completed)
		return;

	std::vector<InventoryHeader> inventories(entities.size());
	SIZE_T headerOffset = 0;
	for (size_t index = 0; index < headerOwners.size(); ++index)
	{
		const size_t owner = headerOwners[index];
		if (headerSucceeded[index * 2] != 0)
			std::memcpy(&inventories[owner].count, headerBuffer.data() + headerOffset, sizeof(int));
		headerOffset += sizeof(int);
		if (headerSucceeded[index * 2 + 1] != 0)
			std::memcpy(&inventories[owner].data, headerBuffer.data() + headerOffset, sizeof(DWORD64));
		headerOffset += sizeof(DWORD64);
	}

	std::vector<MemoryReadRequest> handleRequests;
	std::vector<size_t> handleOwners;
	for (size_t owner = 0; owner < inventories.size(); ++owner)
	{
		const InventoryHeader& inventory = inventories[owner];
		if (inventory.count < 1 || inventory.count > 64 || inventory.data == 0)
			continue;
		for (int item = 0; item < inventory.count; ++item)
		{
			handleRequests.emplace_back(inventory.data + item * sizeof(DWORD), sizeof(DWORD));
			handleOwners.push_back(owner);
		}
	}
	if (handleRequests.empty())
		return;

	std::vector<DWORD> handles(handleRequests.size(), 0);
	std::vector<std::uint8_t> handleSucceeded(handleRequests.size(), 0);
	const MemoryBatchReadResult handleResult = memoryManager.BatchReadMemoryBestEffort(
		handleRequests,
		std::as_writable_bytes(std::span{ handles }),
		MemoryReadPolicy::BypassDataCache,
		handleSucceeded);
	if (!handleResult.completed)
		return;
	for (size_t index = 0; index < handles.size(); ++index)
	{
		if (handleSucceeded[index] == 0)
			handles[index] = 0;
	}

	std::vector<DWORD64> weaponEntities(handles.size(), 0);
	std::vector<std::uint8_t> resolveSucceeded(handles.size(), 0);
	const MemoryBatchReadResult resolveResult = gGame.ResolveEntityHandles(
		handles,
		weaponEntities,
		resolveSucceeded);
	if (!resolveResult.completed)
		return;

	std::vector<MemoryReadRequest> itemRequests;
	std::vector<size_t> itemOwners;
	for (size_t index = 0; index < weaponEntities.size(); ++index)
	{
		if (resolveSucceeded[index] == 0 || weaponEntities[index] == 0)
			continue;
		itemRequests.emplace_back(
			weaponEntities[index] + Offset.EconEntity.AttributeManager +
				Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex,
			sizeof(short));
		itemOwners.push_back(handleOwners[index]);
	}
	if (itemRequests.empty())
		return;

	std::vector<short> itemIndices(itemRequests.size(), -1);
	std::vector<std::uint8_t> itemSucceeded(itemRequests.size(), 0);
	const MemoryBatchReadResult itemResult = memoryManager.BatchReadMemoryBestEffort(
		itemRequests,
		std::as_writable_bytes(std::span{ itemIndices }),
		MemoryReadPolicy::BypassDataCache,
		itemSucceeded);
	if (!itemResult.completed)
		return;
	for (size_t index = 0; index < itemIndices.size(); ++index)
	{
		if (itemSucceeded[index] != 0 && itemIndices[index] == 49)
			entities[itemOwners[index]].second.Pawn.HasC4 = true;
	}
}

bool EntityBatchProcessor::ProcessBoneData(
	std::vector<std::pair<int, CEntity>>& entities)
{
	constexpr SIZE_T BoneCount = static_cast<SIZE_T>(BONEINDEX::ankle_R) + 1;
	constexpr SIZE_T BoneBlockSize = BoneCount * sizeof(BoneMemoryRecord);
	for (auto& [entityIndex, entity] : entities)
		entity.Pawn.BoneData = {};

	std::vector<MemoryReadRequest> sceneRequests;
	sceneRequests.reserve(entities.size());
	for (const auto& [entityIndex, entity] : entities)
		sceneRequests.emplace_back(entity.Pawn.Address + Offset.Pawn.GameSceneNode, sizeof(DWORD64));

	std::vector<DWORD64> sceneNodes(entities.size(), 0);
	std::vector<std::uint8_t> sceneSucceeded(entities.size(), 0);
	const MemoryBatchReadResult sceneResult = memoryManager.BatchReadMemoryBestEffort(
		sceneRequests,
		std::as_writable_bytes(std::span{ sceneNodes }),
		MemoryReadPolicy::BypassDataCache,
		sceneSucceeded);
	if (!sceneResult.completed)
		return false;

	std::vector<MemoryReadRequest> arrayRequests;
	std::vector<size_t> arrayEntityIndices;
	for (size_t index = 0; index < sceneNodes.size(); ++index)
	{
		if (sceneSucceeded[index] == 0 || sceneNodes[index] == 0)
			continue;
		arrayRequests.emplace_back(sceneNodes[index] + Offset.Pawn.BoneArray, sizeof(DWORD64));
		arrayEntityIndices.push_back(index);
	}
	if (arrayRequests.empty())
		return true;

	std::vector<DWORD64> boneArrays(arrayRequests.size(), 0);
	std::vector<std::uint8_t> arraySucceeded(arrayRequests.size(), 0);
	const MemoryBatchReadResult arrayResult = memoryManager.BatchReadMemoryBestEffort(
		arrayRequests,
		std::as_writable_bytes(std::span{ boneArrays }),
		MemoryReadPolicy::BypassDataCache,
		arraySucceeded);
	if (!arrayResult.completed)
		return false;

	std::vector<MemoryReadRequest> blockRequests;
	std::vector<size_t> blockEntityIndices;
	for (size_t index = 0; index < boneArrays.size(); ++index)
	{
		if (arraySucceeded[index] == 0 || boneArrays[index] == 0)
			continue;
		blockRequests.emplace_back(boneArrays[index], BoneBlockSize);
		blockEntityIndices.push_back(arrayEntityIndices[index]);
	}
	if (blockRequests.empty())
		return true;

	std::vector<std::byte> boneBlocks(blockRequests.size() * BoneBlockSize);
	std::vector<std::uint8_t> blockSucceeded(blockRequests.size(), 0);
	const MemoryBatchReadResult blockResult = memoryManager.BatchReadMemoryBestEffort(
		blockRequests,
		boneBlocks,
		MemoryReadPolicy::BypassDataCache,
		blockSucceeded);
	if (!blockResult.completed)
		return false;

	for (size_t index = 0; index < blockRequests.size(); ++index)
	{
		if (blockSucceeded[index] == 0)
			continue;
		const size_t entityIndex = blockEntityIndices[index];
		entities[entityIndex].second.Pawn.BoneData.LoadBoneBlock(
			entities[entityIndex].second.Pawn.Address,
			sceneNodes[entityIndex],
			std::span<const std::byte>(boneBlocks).subspan(index * BoneBlockSize, BoneBlockSize));
	}
	return true;
}
