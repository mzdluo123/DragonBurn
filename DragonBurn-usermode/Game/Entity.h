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
	bool AliveStatus = false;
	bool HasHelmet = false;
	INT64 SteamID = 0;
	int m_nTickBase = 0;
	DWORD Pawn = 0;
	std::string PlayerName;
	std::vector<std::string> spectators = {};


public:
	bool GetTeamID();
	bool GetHealth();
	bool GetIsAlive();
	bool GetPlayerName();
	bool GetPlayerSteamID();
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
	BYTE LifeState = 0xFF;
	bool IsScoped = false;
	float EmitSoundTime = 0.0f;
	bool EmitSoundTimeValid = false;
	bool HasC4 = false;
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
	bool GetIsScoped();
	bool GetShotsFired();
	bool GetAimPunchAngle();
	bool GetHealth();
	bool GetLifeState();
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


	static std::unordered_map<int, std::string> weaponNames;
	static inline std::string GetWeaponName(int weaponID) {
		auto it = weaponNames.find(weaponID);
		if (it != weaponNames.end()) {
			return it->second;
		}
		return weaponID >= 0 ? "weapon_" + std::to_string(weaponID) : "Weapon_None";
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

enum class EntityBatchStatus
{
	Failed,
	Empty,
	Partial,
	Ready
};

class EntityBatchProcessor {
private:
	bool ProcessCoreEntityData(std::vector<std::pair<int, CEntity>>& entities);
	bool ProcessRadarWeaponNames(std::vector<std::pair<int, CEntity>>& entities);
	bool ProcessServiceData(std::vector<std::pair<int, CEntity>>& entities);
	bool ProcessWeaponData(std::vector<std::pair<int, CEntity>>& entities);
	bool ProcessDependenciesData(std::vector<std::pair<int, CEntity>>& entities);
	void ProcessInventoryData(std::vector<std::pair<int, CEntity>>& entities);
	bool ProcessBoneData(std::vector<std::pair<int, CEntity>>& entities);

	std::vector<DWORD64> weaponServiceAddresses_;
	std::vector<DWORD64> aimPunchServiceAddresses_;
	std::vector<DWORD64> cameraAddresses_;
	std::vector<DWORD64> weaponAddresses_;
	bool radarInitialized_ = false;
	size_t radarInputCount_ = 0;

public:
	EntityBatchStatus ProcessRadarEntities(
		std::vector<std::pair<int, CEntity>>& entities,
		std::span<const EntityBatchData> batchData);
	EntityBatchStatus ProcessForegroundEntities(
		std::vector<std::pair<int, CEntity>>& entities,
		bool includeInventory);
};