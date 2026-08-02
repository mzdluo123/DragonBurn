#include "RCS.h"

#include "../Core/Config.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace
{
	constexpr float MouseDegreesPerCount = 0.022f;

	struct RuntimeState
	{
		Vec2 previousCorrection{};
		Vec2 residual{};
	};

	RuntimeState runtimeState;

	bool IsFinite(const Vec2& value) noexcept
	{
		return std::isfinite(value.x) && std::isfinite(value.y);
	}

	bool HasReachedStartBullet(const DWORD shotsFired, const int startBullet) noexcept
	{
		return startBullet <= 0 || shotsFired >= static_cast<DWORD>(startBullet);
	}

	bool HasValidRecoilInput(const CEntity& local) noexcept
	{
		return IsFinite(local.Pawn.AimPunchAngle) && IsFinite(RCS::RCSScale);
	}

	LONG QuantizeMouseAxis(const float movement, float& residual) noexcept
	{
		const double total = static_cast<double>(movement) + static_cast<double>(residual);
		const double clamped = std::clamp(
			total,
			static_cast<double>(std::numeric_limits<LONG>::min()),
			static_cast<double>(std::numeric_limits<LONG>::max()));
		const double whole = std::trunc(clamped);
		residual = static_cast<float>(clamped - whole);
		return static_cast<LONG>(whole);
	}
}

void RCS::ResetRuntime() noexcept
{
	runtimeState = RuntimeState{};
}

Vec2 RCS::GetAimCorrection(const CEntity& local) noexcept
{
	if (!HasReachedStartBullet(local.Pawn.ShotsFired, RCSBullet) || !HasValidRecoilInput(local))
		return {};

	const float yawScale = std::clamp(RCSScale.x, 0.f, 2.f);
	const float pitchScale = std::clamp(RCSScale.y, 0.f, 2.f);
	const Vec2 correction{
		-2.f * local.Pawn.AimPunchAngle.x * pitchScale,
		2.f * local.Pawn.AimPunchAngle.y * yawScale
	};
	return IsFinite(correction) ? correction : Vec2{};
}

void RCS::RecoilControl(const CEntity& local, const bool fireDown, const bool suppressOutput) noexcept
{
	const float sensitivity = local.Client.Sensitivity;
	if (!LegitBotConfig::RCS || !local.IsAlive() || !fireDown ||
		!HasReachedStartBullet(local.Pawn.ShotsFired, RCSBullet) ||
		!std::isfinite(sensitivity) || sensitivity <= 1e-6f || !HasValidRecoilInput(local))
	{
		ResetRuntime();
		return;
	}

	const Vec2 currentCorrection = GetAimCorrection(local);
	if (!IsFinite(currentCorrection))
	{
		ResetRuntime();
		return;
	}

	if (suppressOutput)
	{
		runtimeState.previousCorrection = currentCorrection;
		runtimeState.residual = Vec2{};
		return;
	}

	const Vec2 delta{
		currentCorrection.x - runtimeState.previousCorrection.x,
		currentCorrection.y - runtimeState.previousCorrection.y
	};
	const float countScale = sensitivity * MouseDegreesPerCount;
	const Vec2 counts{ delta.y / countScale, delta.x / countScale };
	if (!std::isfinite(countScale) || countScale <= 0.f || !IsFinite(counts))
	{
		ResetRuntime();
		return;
	}

	const LONG dx = QuantizeMouseAxis(counts.x, runtimeState.residual.x);
	const LONG dy = QuantizeMouseAxis(counts.y, runtimeState.residual.y);
	runtimeState.previousCorrection = currentCorrection;

	if (dx != 0 || dy != 0)
	{
		mouse_event(
			MOUSEEVENTF_MOVE,
			static_cast<DWORD>(dx),
			static_cast<DWORD>(dy),
			0,
			0);
	}
}