#include "Aimbot.h"

#include "../Core/Config.h"
#include "RCS.h"
#include "TriggerBot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <string>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float DegreesPerRadian = 57.29577951308232f;
    constexpr float MouseDegreesPerCount = 0.022f;

    struct RuntimeState
    {
        DWORD64 pawnAddress = 0;
        int hitbox = -1;
        Clock::time_point lastUpdateTime{};
        Clock::time_point nextMoveTime{};
        Clock::time_point reactionUntil{};
        Vec2 jitter{};
        Vec2 residual{};
    };

    RuntimeState runtimeState;
    std::mt19937 randomEngine{ std::random_device{}() };

    bool IsFinite(const Vec2& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool IsFinite(const Vec3& value) noexcept
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool HasReachedStartBullet(const DWORD shotsFired, const int startBullet) noexcept
    {
        return startBullet <= 0 || shotsFired >= static_cast<DWORD>(startBullet);
    }

    bool CheckAutoMode(const std::string& weaponName)
    {
        return weaponName != "deagle" && weaponName != "elite" && weaponName != "fiveseven" &&
            weaponName != "glock" && weaponName != "awp" && weaponName != "xm1014" &&
            weaponName != "mag7" && weaponName != "sawedoff" && weaponName != "tec9" &&
            weaponName != "zeus" && weaponName != "p2000" && weaponName != "nova" &&
            weaponName != "p250" && weaponName != "ssg08" && weaponName != "usp" &&
            weaponName != "revolver";
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

void AimControl::ResetRuntime() noexcept
{
    runtimeState = RuntimeState{};
}

bool AimControl::AimBot(
    const CEntity& local,
    const Vec3& localPos,
    const std::vector<AimCandidate>& candidates)
{
    if (MenuConfig::ShowMenu || candidates.empty() || !local.IsAlive())
    {
        ResetRuntime();
        return false;
    }

    const std::string currentWeapon = TriggerBot::GetWeapon(local);
    if (currentWeapon.empty() || !TriggerBot::CheckWeapon(currentWeapon) ||
        (onlyAuto && !CheckAutoMode(currentWeapon)) ||
        !HasReachedStartBullet(local.Pawn.ShotsFired, AimBullet))
    {
        ResetRuntime();
        return false;
    }

    if (!IgnoreFlash && local.Pawn.FlashDuration > 0.f)
    {
        ResetRuntime();
        return false;
    }

    if (ScopeOnly && TriggerBot::CheckScopeWeapon(currentWeapon))
    {
        bool isScoped = false;
        if (!memoryManager.ReadMemory<bool>(local.Pawn.Address + Offset.Pawn.isScoped, isScoped) || !isScoped)
        {
            ResetRuntime();
            return false;
        }
    }

    const float sensitivity = local.Client.Sensitivity;
    if (!std::isfinite(sensitivity) || sensitivity <= 1e-6f || !IsFinite(localPos) ||
        !IsFinite(local.Pawn.ViewAngle))
    {
        ResetRuntime();
        return false;
    }

    const float configuredMaxFov = std::isfinite(AimFov) ? AimFov : 0.f;
    const float maxFov = std::clamp(configuredMaxFov, 0.f, 179.f);
    const float configuredMinFov = std::isfinite(AimFovMin) ? AimFovMin : 0.f;
    const float minFov = std::clamp(configuredMinFov, 0.f, maxFov);
    if (maxFov <= 0.f)
    {
        ResetRuntime();
        return false;
    }

    const Vec2 recoilCorrection = LegitBotConfig::RCS ? RCS::GetAimCorrection(local) : Vec2{};
    if (!IsFinite(recoilCorrection))
    {
        ResetRuntime();
        return false;
    }

    const AimCandidate* bestCandidate = nullptr;
    float bestPitch = 0.f;
    float bestYaw = 0.f;
    float bestNorm = std::numeric_limits<float>::infinity();

    for (const auto& candidate : candidates)
    {
        if (candidate.pawnAddress == 0 || !IsFinite(candidate.worldPos))
            continue;

        const Vec3 delta = candidate.worldPos - localPos;
        if (!IsFinite(delta))
            continue;

        const float horizontal = std::hypot(delta.x, delta.y);
        const float distance = std::hypot(horizontal, delta.z);
        if (!std::isfinite(horizontal) || !std::isfinite(distance) || distance <= 1e-6f)
            continue;

        const float targetPitch = std::atan2(-delta.z, horizontal) * DegreesPerRadian;
        const float targetYaw = std::atan2(delta.y, delta.x) * DegreesPerRadian;
        const float rawPitch = std::remainder(targetPitch - local.Pawn.ViewAngle.x, 360.f);
        const float rawYaw = std::remainder(targetYaw - local.Pawn.ViewAngle.y, 360.f);
        const float correctedPitch = rawPitch + recoilCorrection.x;
        const float correctedYaw = std::remainder(rawYaw + recoilCorrection.y, 360.f);
        const float norm = std::hypot(correctedPitch, correctedYaw);

        if (!std::isfinite(correctedPitch) || !std::isfinite(correctedYaw) || !std::isfinite(norm) ||
            norm < minFov || norm > maxFov)
            continue;

        if (norm < bestNorm)
        {
            bestCandidate = &candidate;
            bestPitch = correctedPitch;
            bestYaw = correctedYaw;
            bestNorm = norm;
        }
    }

    if (bestCandidate == nullptr)
    {
        ResetRuntime();
        return false;
    }

    const float countScale = sensitivity * MouseDegreesPerCount;
    Vec2 counts{ bestYaw / countScale, bestPitch / countScale };
    if (!std::isfinite(countScale) || countScale <= 0.f || !IsFinite(counts))
    {
        ResetRuntime();
        return false;
    }

    const auto now = Clock::now();
    const bool newTarget = runtimeState.pawnAddress != bestCandidate->pawnAddress ||
        runtimeState.hitbox != bestCandidate->hitbox;
    const float humanization = HumanizeVar
        ? std::clamp(static_cast<float>(HumanizationStrength) / 15.f, 0.f, 1.f)
        : 0.f;

    if (newTarget)
    {
        ResetRuntime();
        runtimeState.pawnAddress = bestCandidate->pawnAddress;
        runtimeState.hitbox = bestCandidate->hitbox;

        if (humanization > 0.f)
        {
            const int maximumReactionMs = static_cast<int>(std::lround(80.f * humanization));
            std::uniform_int_distribution<int> reactionDelay(0, maximumReactionMs);
            runtimeState.reactionUntil = now + std::chrono::milliseconds(reactionDelay(randomEngine));
        }
    }

    if (humanization <= 0.f)
    {
        runtimeState.jitter = Vec2{};
        runtimeState.reactionUntil = Clock::time_point{};
    }

    if (!newTarget && now < runtimeState.nextMoveTime)
        return true;

    const float dt = newTarget
        ? 1.f / 64.f
        : std::clamp(std::chrono::duration<float>(now - runtimeState.lastUpdateTime).count(), 0.001f, 0.050f);
    runtimeState.lastUpdateTime = now;
    runtimeState.nextMoveTime = now + std::chrono::milliseconds(std::clamp(MenuConfig::AimDelay, 1, 50));

    const float configuredSmooth = std::isfinite(Smooth) ? Smooth : 0.f;
    float alpha = 1.f;
    if (configuredSmooth > 0.f)
    {
        const float distanceFactor = 2.f - std::clamp(bestNorm / maxFov, 0.f, 1.f);
        const float tau = 0.010f * std::clamp(configuredSmooth, 0.f, 10.f) * distanceFactor;
        alpha = 1.f - std::exp(-dt / tau);
    }

    const Vec2 baseMove{ counts.x * alpha, counts.y * alpha };
    if (!IsFinite(baseMove))
    {
        ResetRuntime();
        return false;
    }

    if (humanization > 0.f && now < runtimeState.reactionUntil)
        return true;

    Vec2 humanizedMove = baseMove;
    if (humanization > 0.f)
    {
        const float rho = std::exp(-dt / 0.080f);
        const float baseLength = std::hypot(baseMove.x, baseMove.y);
        const float sigma = humanization * std::min(baseLength * 0.03f, 0.35f);
        const float noiseScale = std::sqrt(std::max(0.f, 1.f - rho * rho)) * sigma;
        std::normal_distribution<float> normal(0.f, 1.f);

        runtimeState.jitter.x = rho * runtimeState.jitter.x + noiseScale * normal(randomEngine);
        runtimeState.jitter.y = rho * runtimeState.jitter.y + noiseScale * normal(randomEngine);
        humanizedMove.x += runtimeState.jitter.x;
        humanizedMove.y += runtimeState.jitter.y;

        const float countsLength = std::hypot(counts.x, counts.y);
        const float maximumLength = std::min(countsLength, baseLength * (1.f + 0.15f * humanization));
        const float humanizedLength = std::hypot(humanizedMove.x, humanizedMove.y);
        if (humanizedLength > maximumLength && humanizedLength > 0.f)
        {
            const float lengthScale = maximumLength / humanizedLength;
            humanizedMove.x *= lengthScale;
            humanizedMove.y *= lengthScale;
        }
    }

    if (!IsFinite(humanizedMove))
    {
        ResetRuntime();
        return false;
    }

    const LONG dx = QuantizeMouseAxis(humanizedMove.x, runtimeState.residual.x);
    const LONG dy = QuantizeMouseAxis(humanizedMove.y, runtimeState.residual.y);
    if (dx != 0 || dy != 0)
    {
        mouse_event(
            MOUSEEVENTF_MOVE,
            static_cast<DWORD>(dx),
            static_cast<DWORD>(dy),
            0,
            0);
    }

    return true;
}