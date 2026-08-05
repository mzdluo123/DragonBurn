#include "Bone.h"

#include <cstring>

bool CBone::LoadBoneBlock(
    const DWORD64 entityPawnAddress,
    const DWORD64 gameSceneNodeAddress,
    const std::span<const std::byte> rawBoneBlock)
{
    constexpr size_t BoneCount = static_cast<size_t>(BONEINDEX::ankle_R) + 1;
    constexpr size_t BoneBlockSize = BoneCount * sizeof(BoneMemoryRecord);
    if (entityPawnAddress == 0 || gameSceneNodeAddress == 0 ||
        rawBoneBlock.size() != BoneBlockSize)
    {
        BonePosList.clear();
        IBoneData.clear();
        EntityPawnAddress = 0;
        GameSceneNode = 0;
        return false;
    }

    std::vector<BoneJointPos> bonePositions;
    std::vector<CBoneData> boneData;
    bonePositions.reserve(BoneCount);
    boneData.reserve(BoneCount);

    for (size_t index = 0; index < BoneCount; ++index)
    {
        BoneMemoryRecord record{};
        std::memcpy(
            &record,
            rawBoneBlock.data() + index * sizeof(BoneMemoryRecord),
            sizeof(record));

        const Vec3 position{ record.position[0], record.position[1], record.position[2] };
        const Quaternion_t rotation{
            record.rotation[0],
            record.rotation[1],
            record.rotation[2],
            record.rotation[3]
        };
        Vec2 screenPosition{};
        const bool visible = gGame.View.WorldToScreen(position, screenPosition);
        bonePositions.push_back({ position, screenPosition, visible });
        boneData.push_back({ position, record.scale, rotation });
    }

    EntityPawnAddress = entityPawnAddress;
    GameSceneNode = gameSceneNodeAddress;
    BonePosList = std::move(bonePositions);
    IBoneData = std::move(boneData);
    return true;
}