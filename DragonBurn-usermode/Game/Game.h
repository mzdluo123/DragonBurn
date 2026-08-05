#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include "../Core/MemoryMgr.h"
#include "../Offsets/Offsets.h"
#include "View.h"

struct EntityListCache
{
	DWORD64 root = 0;
	std::array<DWORD64, 64> pages{};
	std::bitset<64> validPages{};
	UINT64 generation = 0;
};

class CGame
{
private:
	struct
	{
		DWORD64 ServerDLL;
		DWORD64 ClientDLL;
		DWORD64 EntityList;
		DWORD64 Matrix;
		DWORD64 ViewAngle;
		EntityListCache entityListCache;
		DWORD64 LocalController;
		DWORD64 LocalPawn;
		DWORD64 ServerPawn;
		DWORD64 GlobalVars;
		DWORD64 JumpBtn;
		DWORD64 AttackBtn;
		DWORD64 RightBtn;
		DWORD64 LeftBtn;
	}Address;

public:
	CView View;

public:	
	bool InitAddress();

	DWORD64 GetClientDLLAddress();
	DWORD64 GetServerDLLAddress();
	DWORD64 GetEntityListAddress();
	DWORD64 GetMatrixAddress();
	DWORD64 GetViewAngleAddress();
	const EntityListCache& GetEntityListCache() const noexcept;
	DWORD64 GetLocalControllerAddress();
	DWORD64 GetLocalPawnAddress();
	DWORD64 GetServerPawnAddress();
	DWORD64 GetGlobalVarsAddress();
	DWORD64 GetJumpBtnAddress();
	DWORD64 GetAttackBtnAddress();
	DWORD64 GetRightBtnAddress();
	DWORD64 GetLeftBtnAddress();

	bool UpdateEntityListCache();
	void InvalidateEntityListCache() noexcept;
	DWORD64 ResolveEntityHandle(DWORD handle) const;
	MemoryBatchReadResult ResolveEntityHandles(
		std::span<const DWORD> handles,
		std::span<DWORD64> addresses,
		std::span<std::uint8_t> requestSucceeded = {}) const;
};

inline CGame gGame;