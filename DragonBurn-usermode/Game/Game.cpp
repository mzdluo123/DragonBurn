#include "Game.h"

#include <cstring>

bool CGame::InitAddress()
{
	InvalidateEntityListCache();
	this->Address.ClientDLL = memoryManager.GetModuleBase(L"client.dll");//MemoryMgr::GetModuleBase(MemoryMgr::GetProcessID(L"cs2.exe"),L"client.dll");
	this->Address.ServerDLL = memoryManager.GetModuleBase(L"server.dll");//MemoryMgr::GetModuleBase(MemoryMgr::GetProcessID(L"cs2.exe"),L"server.dll");
	
	this->Address.EntityList = GetClientDLLAddress() + Offset.EntityList;
	this->Address.Matrix = GetClientDLLAddress() + Offset.Matrix;
	this->Address.ViewAngle = GetClientDLLAddress() + Offset.ViewAngle;
	this->Address.LocalController = GetClientDLLAddress() + Offset.LocalPlayerController;
	this->Address.LocalPawn = GetClientDLLAddress() + Offset.LocalPlayerPawn;
	this->Address.ServerPawn = GetServerDLLAddress() + Offset.LocalPlayerPawn;
	this->Address.GlobalVars = GetClientDLLAddress() + Offset.GlobalVars;
	this->Address.JumpBtn = GetClientDLLAddress() + Offset.Buttons.Jump;
	this->Address.AttackBtn = GetClientDLLAddress() + Offset.Buttons.Attack;
	this->Address.RightBtn = GetClientDLLAddress() + Offset.Buttons.Right;
	this->Address.LeftBtn = GetClientDLLAddress() + Offset.Buttons.Left;

	return this->Address.ClientDLL != 0;
}

DWORD64 CGame::GetClientDLLAddress()
{
	return this->Address.ClientDLL;
}

DWORD64 CGame::GetServerDLLAddress()
{
	return this->Address.ServerDLL;
}

DWORD64 CGame::GetEntityListAddress()
{
	return this->Address.EntityList;
}

DWORD64 CGame::GetMatrixAddress()
{
	return this->Address.Matrix;
}

DWORD64 CGame::GetViewAngleAddress() 
{
	return this->Address.ViewAngle;
}

const EntityListCache& CGame::GetEntityListCache() const noexcept
{
	return this->Address.entityListCache;
}

DWORD64 CGame::GetLocalControllerAddress()
{
	return this->Address.LocalController;
}

DWORD64 CGame::GetLocalPawnAddress()
{
	return this->Address.LocalPawn;
}

DWORD64 CGame::GetServerPawnAddress()
{
	return this->Address.ServerPawn;
}

DWORD64 CGame::GetGlobalVarsAddress()
{
	return this->Address.GlobalVars;
}

DWORD64 CGame::GetJumpBtnAddress()
{
	return this->Address.JumpBtn;
}

DWORD64 CGame::GetAttackBtnAddress()
{
	return this->Address.AttackBtn;
}

DWORD64 CGame::GetRightBtnAddress()
{
	return this->Address.RightBtn;
}

DWORD64 CGame::GetLeftBtnAddress()
{
	return this->Address.LeftBtn;
}


bool CGame::UpdateEntityListCache()
{
	DWORD64 root = 0;
	if (!memoryManager.ReadMemory<DWORD64>(GetEntityListAddress(), root) || root == 0)
	{
		InvalidateEntityListCache();
		return false;
	}

	std::array<MemoryReadRequest, 64> requests{};
	std::array<DWORD64, 64> pages{};
	std::array<std::uint8_t, 64> requestSucceeded{};
	for (size_t pageIndex = 0; pageIndex < requests.size(); ++pageIndex)
		requests[pageIndex] = { root + 0x10 + 8 * pageIndex, sizeof(DWORD64) };

	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ pages }),
		MemoryReadPolicy::BypassDataCache,
		requestSucceeded);
	if (!result.completed || requestSucceeded[0] == 0 || pages[0] == 0)
	{
		InvalidateEntityListCache();
		return false;
	}

	EntityListCache candidate{};
	candidate.root = root;
	for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex)
	{
		if (requestSucceeded[pageIndex] != 0 && pages[pageIndex] != 0)
		{
			candidate.pages[pageIndex] = pages[pageIndex];
			candidate.validPages.set(pageIndex);
		}
	}

	const EntityListCache& current = this->Address.entityListCache;
	const bool unchanged =
		current.root == candidate.root &&
		current.pages == candidate.pages &&
		current.validPages == candidate.validPages;
	candidate.generation = unchanged ? current.generation : current.generation + 1;
	this->Address.entityListCache = candidate;
	return true;
}

void CGame::InvalidateEntityListCache() noexcept
{
	const UINT64 nextGeneration = this->Address.entityListCache.generation + 1;
	this->Address.entityListCache = {};
	this->Address.entityListCache.generation = nextGeneration;
}

DWORD64 CGame::ResolveEntityHandle(const DWORD handle) const
{
	if (handle == 0 || handle == 0xFFFFFFFF)
		return 0;

	const size_t pageIndex = (handle & 0x7FFF) >> 9;
	const size_t slotIndex = handle & 0x1FF;
	const EntityListCache& cache = GetEntityListCache();
	if (pageIndex >= cache.pages.size() || !cache.validPages.test(pageIndex))
		return 0;

	DWORD64 address = 0;
	if (!memoryManager.ReadMemory<DWORD64>(
		cache.pages[pageIndex] + 0x70 * slotIndex,
		address))
	{
		return 0;
	}

	return address;
}

MemoryBatchReadResult CGame::ResolveEntityHandles(
	std::span<const DWORD> handles,
	std::span<DWORD64> addresses,
	std::span<std::uint8_t> requestSucceeded) const
{
	if (addresses.data() != nullptr && !addresses.empty())
		SecureZeroMemory(addresses.data(), addresses.size_bytes());
	if (requestSucceeded.data() != nullptr && !requestSucceeded.empty())
		SecureZeroMemory(requestSucceeded.data(), requestSucceeded.size());

	if (addresses.size() != handles.size() ||
		(!requestSucceeded.empty() && requestSucceeded.size() != handles.size()))
	{
		return {};
	}

	if (handles.empty())
		return { true, 0 };

	const EntityListCache& cache = GetEntityListCache();
	std::vector<MemoryReadRequest> requests;
	std::vector<size_t> originalIndices;
	requests.reserve(handles.size());
	originalIndices.reserve(handles.size());
	for (size_t index = 0; index < handles.size(); ++index)
	{
		const DWORD handle = handles[index];
		if (handle == 0 || handle == 0xFFFFFFFF)
			continue;

		const size_t pageIndex = (handle & 0x7FFF) >> 9;
		const size_t slotIndex = handle & 0x1FF;
		if (pageIndex >= cache.pages.size() || !cache.validPages.test(pageIndex))
			continue;

		requests.push_back({ cache.pages[pageIndex] + 0x70 * slotIndex, sizeof(DWORD64) });
		originalIndices.push_back(index);
	}

	if (requests.empty())
		return { true, 0 };

	std::vector<DWORD64> resolved(requests.size(), 0);
	std::vector<std::uint8_t> resolvedSucceeded(requests.size(), 0);
	const MemoryBatchReadResult result = memoryManager.BatchReadMemoryBestEffort(
		requests,
		std::as_writable_bytes(std::span{ resolved }),
		MemoryReadPolicy::BypassDataCache,
		resolvedSucceeded);
	if (!result.completed)
		return result;

	SIZE_T successfulRequests = 0;
	for (size_t index = 0; index < requests.size(); ++index)
	{
		if (resolvedSucceeded[index] == 0 || resolved[index] == 0)
			continue;

		const size_t originalIndex = originalIndices[index];
		addresses[originalIndex] = resolved[index];
		if (!requestSucceeded.empty())
			requestSucceeded[originalIndex] = 1;
		++successfulRequests;
	}

	return { true, successfulRequests };
}

//bool CGame::GetForceJump(int& value)
//{
//	if (!memoryManager.ReadMemory<int>(this->Address.ForceJump, value))
//		return false;
//
//	return true;
//}
//bool CGame::GetForceCrouch(int& value)
//{
//	if (!memoryManager.ReadMemory<int>(this->Address.ForceCrouch, value))
//		return false;
//
//	return true;
//}
//
//bool CGame::GetForceMove(int MovingType, int& Value)
//{
//	switch (MovingType)
//	{
//	case 0:
//		if (!memoryManager.ReadMemory<int>(this->Address.ForceForward, Value)) return false;
//		break;
//	case 1:
//		if (!memoryManager.ReadMemory<int>(this->Address.ForceLeft, Value)) return false;
//		break;
//	case 2:
//		if (!memoryManager.ReadMemory<int>(this->Address.ForceRight, Value)) return false;
//		break;
//	default:
//		return false;
//		break;
//	}
//	return true;
//}