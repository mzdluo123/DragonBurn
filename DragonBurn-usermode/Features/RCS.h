#pragma once

#include "../Game/Entity.h"

namespace RCS
{
	inline int RCSBullet = 1;
	inline Vec2 RCSScale = { 1.4f, 1.4f };

	Vec2 GetAimCorrection(const CEntity& local) noexcept;
	void RecoilControl(const CEntity& local, bool fireDown, bool suppressOutput) noexcept;
	void ResetRuntime() noexcept;
}
