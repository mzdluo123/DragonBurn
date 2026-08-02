#pragma once

#include "..\Game\Entity.h"

#include <vector>

namespace AimControl
{
    struct AimCandidate
    {
        Vec3 worldPos;
        DWORD64 pawnAddress;
        int hitbox;
    };

    inline int HotKey = VK_LBUTTON;
    inline int AimBullet = 1;
    inline bool ScopeOnly = true;
    inline bool IgnoreFlash = false;
    inline bool HumanizeVar = true;
    inline int HumanizationStrength = 5;
    inline float AimFov = 10.f;
    inline float AimFovMin = 0.4f;
    inline float Smooth = 5.0f;
    inline std::vector<int> HitboxList{ BONEINDEX::head };
    inline bool onlyAuto = false;

    bool AimBot(const CEntity& local, const Vec3& localPos, const std::vector<AimCandidate>& candidates);
    void ResetRuntime() noexcept;
}
