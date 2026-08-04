#pragma once
#include "Game.h"
#include "View.h"
#include "Bone.h"
#include "../Core/Globals.h"

class PlayerController
{
public:
	DWORD64 Address = 0;
	int Money = 0;
	int CashSpent = 0;
	int CashSpentTotal = 0;
	int TeamID = 0;
	int Health = 0;
	int AliveStatus = 0;
	INT64 SteamID = 0;
	int m_nTickBase = 0;
	DWORD Pawn = 0;
	std::string PlayerName;
	std::vector<std::string> spectators = {};

	DWORD64 cachedEntityListEntry = 0;
	DWORD lastCachedPawn = 0;

public:
	bool GetTeamID();
	bool GetHealth();
	bool GetIsAlive();
	bool GetPlayerName();
	bool GetPlayerSteamID();
	DWORD64 GetPlayerPawnAddress();
};

class PlayerPawn
{
public:
	enum class Flags
	{
		NONE,
		ON_GROUND = 1 << 0,
		IN_CROUCH = 1 << 1
	};

	DWORD64 Address = 0;
	CBone BoneData;
	Vec2 ViewAngle;
	Vec3 Pos;
	Vec2 ScreenPos;
	Vec3 CameraPos;
	float Speed;
	std::string WeaponName;
	DWORD ShotsFired;
	DWORD GameSceneNode;
	Vec2 AimPunchAngle{};
	int Health;
	int Ammo;
	//int MaxAmmo;
	int Armor;
	int TeamID;
	int Fov;
	DWORD64 bSpottedByMask;
	int fFlags;
	float FlashDuration;
	//bool isDefusing;

public:
	bool GetPos();
	bool GetViewAngle();
	bool GetCameraPos();
	DWORD64 GetActiveWeaponAddress() const;
	bool GetWeaponName();
	bool GetShotsFired();
	bool GetAimPunchAngle();
	bool GetHealth();
	bool GetTeamID();
	bool GetFov();
	bool GetSpotted();
	bool GetFFlags();
	bool GetAmmo();
	//bool GetMaxAmmo();
	bool GetArmor();
	//bool GetDefusing();
	bool GetFlashDuration();
	bool GetVelocity();

	std::vector<short> GetWeaponInventory(DWORD64 entityList) const;

	bool HasFlag(const Flags Flag) const noexcept
	{
		return fFlags & (int)Flag;
	}
};

class Client
{
public:
	float Sensitivity;

public:
	bool GetSensitivity();
};

class CEntity
{

public:
	PlayerController Controller;
	PlayerPawn Pawn;
	Client Client;

	bool UpdateController(const DWORD64& PlayerControllerAddress);
	bool UpdatePawn(const DWORD64& PlayerPawnAddress);
	bool UpdateRadarPawn(const DWORD64& PlayerPawnAddress);
	bool UpdateClientData();
	bool IsAlive() const;
	bool IsInScreen();
	CBone GetBone() const;

	static DWORD64 ResolveEntityHandle(uint32_t handle);

	static std::unordered_map<int, std::string> weaponNames;
	static inline std::string GetWeaponName(int weaponID) {
		auto it = weaponNames.find(weaponID);
		if (it != weaponNames.end()) {
			return it->second;
		}
		return "Weapon_None";
	}

};

struct EntityBatchData {
	int entityIndex;
	DWORD64 controllerAddress;
	DWORD64 pawnAddress;

	EntityBatchData(int idx, DWORD64 ctrlAddr, DWORD64 pawnAddr)
		: entityIndex(idx), controllerAddress(ctrlAddr), pawnAddress(pawnAddr) {
	}
};

class EntityBatchProcessor {
private:
	std::vector<EntityBatchData> entityBatchData;
	std::vector<std::pair<DWORD64, SIZE_T>> allRequests;
	std::vector<BYTE> masterBuffer;

	// Phase 1: Controller + core pawn data
	bool ProcessCoreEntityData(std::vector<std::pair<int, CEntity>>& entities,
		std::vector<DWORD64>& weaponServiceAddresses,
		std::vector<DWORD64>& aimPunchServiceAddresses,
		std::vector<DWORD64>& cameraAddresses);

	// Phase 2: Pointer-dependent pawn data
	bool ProcessServiceData(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<DWORD64>& weaponServiceAddresses,
		const std::vector<DWORD64>& aimPunchServiceAddresses,
		std::vector<DWORD64>& weaponAddresses);

	// Phase 3: Weapon data
	bool ProcessWeaponData(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<DWORD64>& weaponAddresses,
		std::vector<DWORD64>& weaponDataAddresses);

	// Phase 4: Final dependent data
	bool ProcessDependenciesData(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<DWORD64>& weaponDataAddresses,
		const std::vector<DWORD64>& cameraAddresses);

public:

	bool ProcessRadarEntities(std::vector<std::pair<int, CEntity>>& entities,
		const std::vector<EntityBatchData>& batchData);
	bool ProcessAllEntities(std::vector<std::pair<int, CEntity>>& entities, const std::vector<EntityBatchData>& batchData);
};