#include "Entity.h"
#undef min()
#undef max()


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

	this->Pawn.Address = this->Controller.GetPlayerPawnAddress();

	return true;
}

bool CEntity::UpdatePawn(const DWORD64& PlayerPawnAddress)
{
	if (PlayerPawnAddress == 0)
		return false;
	this->Pawn.Address = PlayerPawnAddress;

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
	if (!this->Pawn.BoneData.UpdateAllBoneData(PlayerPawnAddress))//
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
	return GetDataAddressWithOffset<int>(Address, Offset.Entity.IsAlive, this->AliveStatus);
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

	return CEntity::ResolveEntityHandle(activeWeaponHandle);
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


DWORD64 PlayerController::GetPlayerPawnAddress()
{
	// Get current Pawn ID
	if (!GetDataAddressWithOffset<DWORD>(Address, Offset.Entity.PlayerPawn, this->Pawn))
		return 0;

	// Pre-calculate indices
	const DWORD64 highIndex = (Pawn & 0x7FFF) >> 9;
	const DWORD64 lowIndex = Pawn & 0x1FF;

	// Cache the entity list entry if it's the same high-level index
	DWORD64 EntityPawnListEntry = 0;

	if (cachedEntityListEntry == 0 || (lastCachedPawn & 0x7FFF) >> 9 != highIndex) {
		// Need to refresh cache
		if (!memoryManager.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), EntityPawnListEntry))
			return 0;

		if (EntityPawnListEntry == 0) return 0;

		if (!memoryManager.ReadMemory<DWORD64>(EntityPawnListEntry + 0x10 + 8 * highIndex, cachedEntityListEntry))
			return 0;

		lastCachedPawn = Pawn;
	}

	if (cachedEntityListEntry == 0) return 0;

	// Final read using cached value
	DWORD64 EntityPawnAddress = 0;
	if (!memoryManager.ReadMemory<DWORD64>(cachedEntityListEntry + 0x70 * lowIndex, EntityPawnAddress))
		return 0;

	return EntityPawnAddress;
}

bool PlayerPawn::GetPos()
{
	return GetDataAddressWithOffset<Vec3>(Address, Offset.Pawn.Pos, this->Pos);
}

bool PlayerPawn::GetHealth()
{
	return GetDataAddressWithOffset<int>(Address, Offset.Pawn.CurrentHealth, this->Health);
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

std::vector<short> PlayerPawn::GetWeaponInventory(DWORD64 entityList) const
{
	std::vector<short> weapons;

	DWORD64 weaponServices = 0;
	if (!memoryManager.ReadMemory<DWORD64>(Address + Offset.Pawn.m_pWeaponServices, weaponServices))
		return weapons;
	if (weaponServices == 0)
		return weapons;

	int weaponsCount = 0;
	if (!memoryManager.ReadMemory<int>(weaponServices + Offset.WeaponBaseData.hMyWeapons, weaponsCount))
		return weapons;
	if (weaponsCount <= 0 || weaponsCount > 64)
		return weapons;

	DWORD64 weaponsData = 0;
	if (!memoryManager.ReadMemory<DWORD64>(weaponServices + Offset.WeaponBaseData.hMyWeapons + 0x8, weaponsData))
		return weapons;
	if (weaponsData == 0)
		return weapons;

	for (int i = 0; i < weaponsCount; i++)
	{
		DWORD64 weaponHandleOffset = weaponsData + (i * 0x4);

		DWORD weaponHandle = 0;
		if (!memoryManager.ReadMemory<DWORD>(weaponHandleOffset, weaponHandle))
			continue;
		if (weaponHandle == 0 || weaponHandle == 0xFFFFFFFF)
			continue;

		int entityIndex = weaponHandle & 0x7FFF;
		if (entityIndex >= 0x4000)
			continue;

		DWORD64 weaponPtr = CEntity::ResolveEntityHandle(weaponHandle);
		if (weaponPtr == 0)
			continue;

		short weaponID = 0;
		if (!memoryManager.ReadMemory<short>(weaponPtr + Offset.EconEntity.AttributeManager +
			Offset.WeaponBaseData.Item +
			Offset.WeaponBaseData.ItemDefinitionIndex, weaponID))
			continue;
		if (weaponID > 0 && weaponID < 100)
			weapons.push_back(weaponID);
	}

	return weapons;
}

bool CEntity::IsAlive() const
{
	return this->Controller.AliveStatus == 1 && this->Pawn.Health > 0;
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

DWORD64 CEntity::ResolveEntityHandle(uint32_t handle)
{
	static DWORD64 cachedEntityListEntry = 0;
	static DWORD lastCachedPawn = 0;

	if (handle == 0)
		return 0;

	// Pre-calculate indices
	const DWORD64 highIndex = (handle & 0x7FFF) >> 9;
	const DWORD64 lowIndex = handle & 0x1FF;

	// Cache the entity list entry if it's the same high-level index
	DWORD64 EntityListEntry = 0;

	if (cachedEntityListEntry == 0 || (lastCachedPawn & 0x7FFF) >> 9 != highIndex) {
		// Need to refresh cache
		if (!memoryManager.ReadMemory<DWORD64>(gGame.GetEntityListAddress(), EntityListEntry))
			return 0;

		if (EntityListEntry == 0) return 0;

		if (!memoryManager.ReadMemory<DWORD64>(EntityListEntry + 0x10 + 8 * highIndex, cachedEntityListEntry))
			return 0;

		lastCachedPawn = handle;
	}

	if (cachedEntityListEntry == 0) return 0;

	// Final read using cached value
	DWORD64 EntityAddress = 0;
	if (!memoryManager.ReadMemory<DWORD64>(cachedEntityListEntry + 0x70 * lowIndex, EntityAddress))
		return 0;

	return EntityAddress;
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

bool EntityBatchProcessor::ProcessRadarEntities(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::vector<EntityBatchData>& batchData) {

	if (batchData.empty()) return false;

	entities.clear();
	entities.reserve(batchData.size());

	// Initialize entities
	for (const auto& data : batchData) {
		CEntity entity;
		entity.Controller.Address = data.controllerAddress;
		entity.Pawn.Address = data.pawnAddress;
		entities.emplace_back(data.entityIndex, std::move(entity));
	}

	std::vector<DWORD64> weaponServiceAddresses, aimPunchServiceAddresses, cameraAddresses;
	if (!ProcessCoreEntityData(entities, weaponServiceAddresses, aimPunchServiceAddresses, cameraAddresses)) {
		entities.clear();
		return false;
	}

	return true;
}

bool EntityBatchProcessor::ProcessAllEntities(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::vector<EntityBatchData>& batchData) {

	if (batchData.empty()) return false;

	entities.clear();
	entities.reserve(batchData.size());

	// Initialize entities
	for (const auto& data : batchData) {
		CEntity entity;
		entity.Controller.Address = data.controllerAddress;
		entity.Pawn.Address = data.pawnAddress;
		entities.emplace_back(data.entityIndex, std::move(entity));
	}

	std::vector<DWORD64> weaponServiceAddresses, aimPunchServiceAddresses;
	std::vector<DWORD64> weaponAddresses, cameraAddresses, weaponDataAddresses;

	// Phase 1: Controller + Core Pawn Data
	if (!ProcessCoreEntityData(entities, weaponServiceAddresses, aimPunchServiceAddresses, cameraAddresses)) {
		return false;
	}

	// Phase 2: Pointer-dependent Pawn Data
	if (!ProcessServiceData(entities, weaponServiceAddresses, aimPunchServiceAddresses, weaponAddresses)) {
		return false;
	}

	// Phase 3: Weapon Data
	if (!ProcessWeaponData(entities, weaponAddresses, weaponDataAddresses)) {
		return false;
	}

	// Phase 4: Final Dependent Data
	if (!ProcessDependenciesData(entities, weaponDataAddresses, cameraAddresses)) {
		return false;
	}
	// Phase 5: Bone Data. A stale pawn must not leave an empty bone vector
	// for render and target-selection code to index.
	std::erase_if(entities, [](auto& indexedEntity) {
		auto& pawn = indexedEntity.second.Pawn;
		return pawn.Address == 0 || !pawn.BoneData.UpdateAllBoneData(pawn.Address);
	});

	return true;
}

bool EntityBatchProcessor::ProcessCoreEntityData(
	std::vector<std::pair<int, CEntity>>& entities,
	std::vector<DWORD64>& weaponServiceAddresses,
	std::vector<DWORD64>& aimPunchServiceAddresses,
	std::vector<DWORD64>& cameraAddresses) {

	std::vector<std::pair<DWORD64, SIZE_T>> requests;
	requests.reserve(entities.size() * 20); // 6 controller + 14 pawn fields per entity

	// Build all requests for Phase 1
	for (const auto& [entityIndex, entity] : entities) {
		// ALL controller requests for this entity
		requests.emplace_back(entity.Controller.Address + Offset.Pawn.CurrentHealth, sizeof(int));
		requests.emplace_back(entity.Controller.Address + Offset.Entity.IsAlive, sizeof(int));
		requests.emplace_back(entity.Controller.Address + Offset.Pawn.iTeamNum, sizeof(int));
		requests.emplace_back(entity.Controller.Address + Offset.Entity.iszPlayerName, MAX_PATH);
		requests.emplace_back(entity.Controller.Address + Offset.PlayerController.m_steamID, sizeof(INT64));
		//requests.emplace_back(entity.Controller.Address + Offset.PlayerController.m_nTickBase, sizeof(DWORD64));
		requests.emplace_back(entity.Controller.Address + Offset.Entity.PlayerPawn, sizeof(DWORD));

		// ALL pawn requests for this entity
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.angEyeAngles, sizeof(Vec2));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.LastCameraSetupLocalOrigin, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.Pos, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.bSpottedByMask, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.fFlags, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.iShotsFired, sizeof(DWORD));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.iTeamNum, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CurrentHealth, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CurrentArmor, sizeof(int));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.flFlashDuration, sizeof(float));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.AbsVelocity, sizeof(Vec3));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.m_pWeaponServices, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.CameraServices, sizeof(DWORD64));
		requests.emplace_back(entity.Pawn.Address + Offset.Pawn.AimPunchServices, sizeof(DWORD64));
	}

	// Calculate total buffer size
	SIZE_T totalSize = 0;
	for (const auto& req : requests) {
		totalSize += req.second;
	}

	std::vector<BYTE> buffer(totalSize);
	if (!memoryManager.BatchReadMemoryBestEffort(requests, buffer.data())) {
		return false;
	}

	weaponServiceAddresses.clear();
	aimPunchServiceAddresses.clear();
	cameraAddresses.clear();
	weaponServiceAddresses.reserve(entities.size());
	aimPunchServiceAddresses.reserve(entities.size());
	cameraAddresses.reserve(entities.size());


	SIZE_T currentOffset = 0;

	for (auto& [entityIndex, entity] : entities) {

		// Extract controller data (6 fields)
		memcpy(&entity.Controller.Health, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Controller.AliveStatus, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Controller.TeamID, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		char tempName[MAX_PATH] = { 0 };
		memcpy(tempName, buffer.data() + currentOffset, MAX_PATH);
		entity.Controller.PlayerName = std::string(tempName);
		if (entity.Controller.PlayerName.empty()) {
			entity.Controller.PlayerName = "Name_None";
		}
		currentOffset += MAX_PATH;

		memcpy(&entity.Controller.SteamID, buffer.data() + currentOffset, sizeof(INT64));
		currentOffset += sizeof(INT64);

		/*memcpy(&entity.Controller.m_nTickBase, buffer.data() + currentOffset, sizeof(DWORD));
		currentOffset += sizeof(DWORD);*/

		memcpy(&entity.Controller.Pawn, buffer.data() + currentOffset, sizeof(DWORD));
		currentOffset += sizeof(DWORD);

		// Extract pawn core data (14 fields)
		memcpy(&entity.Pawn.ViewAngle, buffer.data() + currentOffset, sizeof(Vec2));
		currentOffset += sizeof(Vec2);

		memcpy(&entity.Pawn.CameraPos, buffer.data() + currentOffset, sizeof(Vec3));
		currentOffset += sizeof(Vec3);

		memcpy(&entity.Pawn.Pos, buffer.data() + currentOffset, sizeof(Vec3));
		currentOffset += sizeof(Vec3);

		memcpy(&entity.Pawn.bSpottedByMask, buffer.data() + currentOffset, sizeof(DWORD64));
		currentOffset += sizeof(DWORD64);

		memcpy(&entity.Pawn.fFlags, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Pawn.ShotsFired, buffer.data() + currentOffset, sizeof(DWORD));
		currentOffset += sizeof(DWORD);

		memcpy(&entity.Pawn.TeamID, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Pawn.Health, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Pawn.Armor, buffer.data() + currentOffset, sizeof(int));
		currentOffset += sizeof(int);

		memcpy(&entity.Pawn.FlashDuration, buffer.data() + currentOffset, sizeof(float));
		currentOffset += sizeof(float);

		//memcpy(&entity.Pawn.isDefusing, buffer.data() + currentOffset, sizeof(bool));
		//currentOffset += sizeof(bool);

		// Calculate velocity
		Vec3 velocity;
		memcpy(&velocity, buffer.data() + currentOffset, sizeof(Vec3));
		entity.Pawn.Speed = sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
		currentOffset += sizeof(Vec3);

		// Extract dependent service addresses
		DWORD64 weaponServiceAddr, cameraAddr, aimPunchServiceAddr;
		memcpy(&weaponServiceAddr, buffer.data() + currentOffset, sizeof(DWORD64));
		currentOffset += sizeof(DWORD64);

		memcpy(&cameraAddr, buffer.data() + currentOffset, sizeof(DWORD64));
		currentOffset += sizeof(DWORD64);

		memcpy(&aimPunchServiceAddr, buffer.data() + currentOffset, sizeof(DWORD64));
		currentOffset += sizeof(DWORD64);

		weaponServiceAddresses.push_back(weaponServiceAddr);
		aimPunchServiceAddresses.push_back(aimPunchServiceAddr);
		cameraAddresses.push_back(cameraAddr);
	}

	return true;
}

bool EntityBatchProcessor::ProcessServiceData(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::vector<DWORD64>& weaponServiceAddresses,
	const std::vector<DWORD64>& aimPunchServiceAddresses,
	std::vector<DWORD64>& weaponAddresses) {

	std::vector<std::pair<DWORD64, SIZE_T>> requests;
	std::vector<std::pair<size_t, bool>> requestMap; // entity index, active weapon request
	requests.reserve(entities.size() * 2);
	requestMap.reserve(entities.size() * 2);
	weaponAddresses.assign(entities.size(), 0);

	for (size_t i = 0; i < entities.size(); ++i) {
		entities[i].second.Pawn.AimPunchAngle = Vec2();

		if (weaponServiceAddresses[i] != 0) {
			requests.emplace_back(weaponServiceAddresses[i] + Offset.WeaponBaseData.hActiveWeapon, sizeof(DWORD));
			requestMap.emplace_back(i, true);
		}

		if (aimPunchServiceAddresses[i] != 0) {
			requests.emplace_back(aimPunchServiceAddresses[i] + Offset.Pawn.PredictableAimPunchAngle, sizeof(Vec2));
			requestMap.emplace_back(i, false);
		}
	}

	if (requests.empty())
		return true;

	SIZE_T totalSize = 0;
	for (const auto& request : requests)
		totalSize += request.second;

	std::vector<BYTE> buffer(totalSize);
	if (!memoryManager.BatchReadMemoryBestEffort(requests, buffer.data()))
		return false;

	SIZE_T bufferOffset = 0;
	for (const auto& [entityIndex, isActiveWeapon] : requestMap) {
		if (isActiveWeapon) {
			DWORD activeWeaponHandle = 0;
			memcpy(&activeWeaponHandle, buffer.data() + bufferOffset, sizeof(DWORD));
			bufferOffset += sizeof(DWORD);

			if (activeWeaponHandle != 0 && activeWeaponHandle != 0xFFFFFFFF)
				weaponAddresses[entityIndex] = CEntity::ResolveEntityHandle(activeWeaponHandle);
		}
		else {
			memcpy(&entities[entityIndex].second.Pawn.AimPunchAngle,
				buffer.data() + bufferOffset, sizeof(Vec2));
			bufferOffset += sizeof(Vec2);
		}
	}

	return true;
}

bool EntityBatchProcessor::ProcessWeaponData(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::vector<DWORD64>& weaponAddresses,
	std::vector<DWORD64>& weaponDataAddresses) {

	std::vector<std::pair<DWORD64, SIZE_T>> requests;
	std::vector<size_t> validWeaponIndices;

	// Build requests only for valid weapon addresses
	for (size_t i = 0; i < weaponAddresses.size(); ++i) {
		if (weaponAddresses[i] != 0) {
			validWeaponIndices.push_back(i);
			requests.emplace_back(weaponAddresses[i] + Offset.WeaponBaseData.Clip1, sizeof(int));
			requests.emplace_back(weaponAddresses[i] + Offset.WeaponBaseData.WeaponDataPTR, sizeof(DWORD64));
			requests.emplace_back(weaponAddresses[i] + Offset.EconEntity.AttributeManager +
				Offset.WeaponBaseData.Item + Offset.WeaponBaseData.ItemDefinitionIndex, sizeof(short));
		}
	}

	weaponDataAddresses.clear();
	weaponDataAddresses.resize(entities.size(), 0);

	if (requests.empty()) {
		// No valid weapons, set defaults
		for (auto& [entityIndex, entity] : entities) {
			entity.Pawn.Ammo = 0;
			entity.Pawn.WeaponName = "Weapon_None";
		}
		return true;
	}

	SIZE_T totalSize = 0;
	for (const auto& req : requests) {
		totalSize += req.second;
	}

	std::vector<BYTE> buffer(totalSize);
	if (!memoryManager.BatchReadMemoryBestEffort(requests, buffer.data())) {
		return false;
	}


	// Extract weapon data
	SIZE_T bufferOffset = 0;
	const SIZE_T WEAPON_DATA_SIZE = sizeof(int) + sizeof(DWORD64) + sizeof(short);

	for (size_t i = 0; i < validWeaponIndices.size(); ++i) {
		size_t entityIndex = validWeaponIndices[i];
		auto& entity = entities[entityIndex].second;

		// Extract ammo
		int ammo;
		memcpy(&ammo, buffer.data() + bufferOffset, sizeof(int));
		entity.Pawn.Ammo = ammo;
		bufferOffset += sizeof(int);

		// Extract weapon data address
		DWORD64 weaponDataAddr;
		memcpy(&weaponDataAddr, buffer.data() + bufferOffset, sizeof(DWORD64));
		weaponDataAddresses[entityIndex] = weaponDataAddr;
		bufferOffset += sizeof(DWORD64);

		// Extract weapon index and set name
		short weaponIndex;
		memcpy(&weaponIndex, buffer.data() + bufferOffset, sizeof(short));
		bufferOffset += sizeof(short);

		if (weaponIndex != -1) {
			auto it = CEntity::weaponNames.find(weaponIndex);
			entity.Pawn.WeaponName = (it != CEntity::weaponNames.end()) ? it->second : "Weapon_None";
		}
		else {
			entity.Pawn.WeaponName = "Weapon_None";
		}
	}

	// Set defaults for entities without weapons
	for (size_t i = 0; i < entities.size(); ++i) {
		if (weaponAddresses[i] == 0) {
			entities[i].second.Pawn.Ammo = 0;
			entities[i].second.Pawn.WeaponName = "Weapon_None";
		}
	}
	return true;
}

bool EntityBatchProcessor::ProcessDependenciesData(
	std::vector<std::pair<int, CEntity>>& entities,
	const std::vector<DWORD64>& weaponDataAddresses,
	const std::vector<DWORD64>& cameraAddresses) {

	std::vector<std::pair<DWORD64, SIZE_T>> requests;
	std::vector/*<std::pair*/<size_t/*, bool*/> requestMap; // entityIndex, isMaxAmmo

	// Build requests for max ammo and FOV
	for (size_t i = 0; i < entities.size(); ++i) {
		//if (weaponDataAddresses[i] != 0) {
			//requests.emplace_back(weaponDataAddresses[i] + Offset.WeaponBaseData.MaxClip, sizeof(int));
			//requestMap.emplace_back(i, true); // true = maxAmmo
		//}
		if (cameraAddresses[i] != 0) {
			requests.emplace_back(cameraAddresses[i] + Offset.Pawn.iFovStart, sizeof(int));
			requestMap.emplace_back(i/*, false*/); // false = FOV
		}
	}

	if (requests.empty()) {
		// Set defaults
		for (auto& [entityIndex, entity] : entities) {
			//entity.Pawn.MaxAmmo = 0;
			entity.Pawn.Fov = 90;
		}
		return true;
	}

	SIZE_T totalSize = 0;
	for (const auto& req : requests) {
		totalSize += req.second;
	}

	std::vector<BYTE> buffer(totalSize);
	if (!memoryManager.BatchReadMemoryBestEffort(requests, buffer.data())) {
		return false;
	}

	// Initialize defaults
	for (auto& [entityIndex, entity] : entities) {
		//entity.Pawn.MaxAmmo = 0;
		entity.Pawn.Fov = 90;
	}

	// Extract final data
	SIZE_T bufferOffset = 0;
	for (const auto& entityIndex/*[entityIndex, isMaxAmmo]*/ : requestMap) {
		auto& entity = entities[entityIndex].second;

		int value;
		memcpy(&value, buffer.data() + bufferOffset, sizeof(int));
		bufferOffset += sizeof(int);

		//if (isMaxAmmo) {
		//	entity.Pawn.MaxAmmo = value;
		//}
		//else {
			entity.Pawn.Fov = value;
		//}
	}
	return true;
}
