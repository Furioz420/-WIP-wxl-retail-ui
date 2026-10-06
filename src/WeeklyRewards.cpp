// Retail C_WeeklyRewards state cache backed by WarcraftXL custom packets.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace
{
    namespace luaoff = wxl::offsets::engine::lua;

    namespace opcodes
    {
        constexpr uint16_t CmsgWeeklyRewardsRequestState = 0x053A;
        constexpr uint16_t SmsgWeeklyRewardsState = 0x053B;
    }

    constexpr uint8_t kProtocolVersion = 5;
    constexpr size_t kActivityWireBytes = 36;
    constexpr uintptr_t kLuaCheckStack = 0x0084DAB0;
    using LuaCheckStackFn = int(__cdecl*)(void* state, int extra);

    struct Activity
    {
        uint8_t type = 0;
        uint8_t index = 0;
        uint16_t threshold = 0;
        uint16_t progress = 0;
        uint16_t level = 0;
        uint32_t id = 0;
        uint32_t activityTierId = 0;
        uint32_t claimId = 0;
        uint32_t mapId = 0;
        uint32_t rewardItemId = 0;
        uint16_t targetItemLevel = 0;
        uint8_t lootSpecId = 0;
        uint8_t flags = 0;
        uint32_t sourceItemId = 0;
    };

    struct State
    {
        bool valid = false;
        uint32_t revision = 0;
        uint32_t periodId = 0;
        uint32_t periodStart = 0;
        uint32_t periodEnd = 0;
        uint8_t flags = 0;
        uint8_t currentLootSpecId = 0;
        uint32_t essenceItemId = 0;
        uint32_t essenceAmount = 0;
        std::vector<Activity> activities;
    };

    std::mutex g_stateMutex;
    State g_state;

    template <typename T>
    bool Read(std::span<const uint8_t> payload, size_t& cursor, T& value)
    {
        if (cursor > payload.size() || sizeof(T) > payload.size() - cursor)
            return false;
        std::memcpy(&value, payload.data() + cursor, sizeof(T));
        cursor += sizeof(T);
        return true;
    }

    void OnState(const uint8_t* data, uint32_t size, void*)
    {
        const std::span<const uint8_t> payload(data, size);
        size_t cursor = 0;
        uint8_t version = 0;
        uint8_t activityCount = 0;
        State next;
        if (!Read(payload, cursor, version) || version != kProtocolVersion ||
            !Read(payload, cursor, next.revision) ||
            !Read(payload, cursor, next.periodId) ||
            !Read(payload, cursor, next.periodStart) ||
            !Read(payload, cursor, next.periodEnd) ||
            !Read(payload, cursor, next.flags) ||
            !Read(payload, cursor, next.currentLootSpecId) ||
            !Read(payload, cursor, next.essenceItemId) ||
            !Read(payload, cursor, next.essenceAmount) ||
            !Read(payload, cursor, activityCount) || activityCount > 16 ||
            payload.size() - cursor != size_t(activityCount) * kActivityWireBytes)
        {
            WLOG_WARN("weekly-rewards: rejected malformed state bytes=%u", size);
            return;
        }

        next.activities.reserve(activityCount);
        for (uint8_t i = 0; i < activityCount; ++i)
        {
            Activity activity;
            if (!Read(payload, cursor, activity.type) ||
                !Read(payload, cursor, activity.index) ||
                !Read(payload, cursor, activity.threshold) ||
                !Read(payload, cursor, activity.progress) ||
                !Read(payload, cursor, activity.level) ||
                !Read(payload, cursor, activity.id) ||
                !Read(payload, cursor, activity.activityTierId) ||
                !Read(payload, cursor, activity.claimId) ||
                !Read(payload, cursor, activity.mapId) ||
                !Read(payload, cursor, activity.rewardItemId) ||
                !Read(payload, cursor, activity.targetItemLevel) ||
                !Read(payload, cursor, activity.lootSpecId) ||
                !Read(payload, cursor, activity.flags) ||
                !Read(payload, cursor, activity.sourceItemId) ||
                activity.index == 0 || activity.threshold == 0)
            {
                WLOG_WARN("weekly-rewards: rejected malformed activity index=%u", i);
                return;
            }
            next.activities.push_back(activity);
        }

        next.valid = true;
        {
            const std::lock_guard lock(g_stateMutex);
            g_state = std::move(next);
        }

        const WXL_FrameScriptApi* api = wxl_retail_ui::FrameScript();
        if (api)
            api->Execute(
                "if wxlwow and wxlwow.weekly_rewards and "
                "wxlwow.weekly_rewards._NativeChanged then "
                "wxlwow.weekly_rewards._NativeChanged() end",
                "weekly-rewards-update");
    }

    bool SendAction(uint8_t action, uint32_t claimId = 0)
    {
        std::array<uint8_t, 6> payload{};
        payload[0] = kProtocolVersion;
        payload[1] = action;
        uint32_t size = 2;
        if (action == 3)
        {
            std::memcpy(payload.data() + 2, &claimId, sizeof(claimId));
            size = static_cast<uint32_t>(payload.size());
        }
        const WXL_NetworkApi* api = wxl_retail_ui::Network();
        return api && api->Send(
            opcodes::CmsgWeeklyRewardsRequestState, payload.data(),
            size) != 0;
    }

    int PushSendResult(void* state, bool sent)
    {
        wxl::game::Native<luaoff::LuaPushBooleanFn>(
            luaoff::kLuaPushBoolean)(state, sent ? 1 : 0);
        return 1;
    }

    int __cdecl LuaRequestState(void* state) { return PushSendResult(state, SendAction(0)); }
    int __cdecl LuaOpenInteraction(void* state) { return PushSendResult(state, SendAction(1)); }
    int __cdecl LuaCloseInteraction(void* state) { return PushSendResult(state, SendAction(2)); }
    int __cdecl LuaGenerateRewards(void* state) { return PushSendResult(state, SendAction(4)); }
    int __cdecl LuaClaimEssence(void* state) { return PushSendResult(state, SendAction(5)); }

    int __cdecl LuaClaimReward(void* state)
    {
        const auto getTop = wxl::game::Native<luaoff::LuaGetTopFn>(luaoff::kLuaGetTop);
        const auto toNumber = wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber);
        if (!getTop || !toNumber || getTop(state) < 1)
            return PushSendResult(state, false);
        const double rawClaimId = toNumber(state, 1);
        if (rawClaimId < 1.0 || rawClaimId > 4294967295.0)
            return PushSendResult(state, false);
        return PushSendResult(state, SendAction(3, static_cast<uint32_t>(rawClaimId)));
    }

    int __cdecl LuaGetState(void* state)
    {
        State snapshot;
        {
            const std::lock_guard lock(g_stateMutex);
            snapshot = g_state;
        }

        const int resultCount = 10 + static_cast<int>(snapshot.activities.size()) * 14;
        const auto checkStack = wxl::game::Native<LuaCheckStackFn>(kLuaCheckStack);
        if (!checkStack || !checkStack(state, resultCount))
        {
            wxl::game::Native<luaoff::LuaPushBooleanFn>(
                luaoff::kLuaPushBoolean)(state, 0);
            WLOG_WARN("weekly-rewards: failed to reserve Lua result stack count=%d", resultCount);
            return 1;
        }

        const auto pushBoolean = wxl::game::Native<luaoff::LuaPushBooleanFn>(
            luaoff::kLuaPushBoolean);
        const auto pushNumber = wxl::game::Native<luaoff::LuaPushNumberFn>(
            luaoff::kLuaPushNumber);
        pushBoolean(state, snapshot.valid ? 1 : 0);
        pushNumber(state, snapshot.revision);
        pushNumber(state, snapshot.periodId);
        pushNumber(state, snapshot.periodStart);
        pushNumber(state, snapshot.periodEnd);
        pushNumber(state, snapshot.flags);
        pushNumber(state, snapshot.currentLootSpecId);
        pushNumber(state, snapshot.essenceItemId);
        pushNumber(state, snapshot.essenceAmount);
        pushNumber(state, static_cast<double>(snapshot.activities.size()));
        for (const Activity& activity : snapshot.activities)
        {
            pushNumber(state, activity.type);
            pushNumber(state, activity.index);
            pushNumber(state, activity.threshold);
            pushNumber(state, activity.progress);
            pushNumber(state, activity.level);
            pushNumber(state, activity.id);
            pushNumber(state, activity.activityTierId);
            pushNumber(state, activity.claimId);
            pushNumber(state, activity.mapId);
            pushNumber(state, activity.rewardItemId);
            pushNumber(state, activity.targetItemLevel);
            pushNumber(state, activity.lootSpecId);
            pushNumber(state, activity.flags);
            pushNumber(state, activity.sourceItemId);
        }
        return resultCount;
    }

    constexpr char kBootstrap[] = R"lua(
do
    wxlwow = wxlwow or {}
    wxlwow.weekly_rewards = wxlwow.weekly_rewards or {}
    local WR = wxlwow.weekly_rewards

    if _WXLWOW_WEEKLY_REWARDS_REQUEST_STATE then
        WR.RequestState = _WXLWOW_WEEKLY_REWARDS_REQUEST_STATE
        _WXLWOW_WEEKLY_REWARDS_REQUEST_STATE = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_GET_STATE then
        WR.GetNativeState = _WXLWOW_WEEKLY_REWARDS_GET_STATE
        _WXLWOW_WEEKLY_REWARDS_GET_STATE = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_OPEN then
        WR.OpenInteraction = _WXLWOW_WEEKLY_REWARDS_OPEN
        _WXLWOW_WEEKLY_REWARDS_OPEN = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_CLOSE then
        WR.CloseInteraction = _WXLWOW_WEEKLY_REWARDS_CLOSE
        _WXLWOW_WEEKLY_REWARDS_CLOSE = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_CLAIM then
        WR.ClaimReward = _WXLWOW_WEEKLY_REWARDS_CLAIM
        _WXLWOW_WEEKLY_REWARDS_CLAIM = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_GENERATE then
        WR.GenerateRewards = _WXLWOW_WEEKLY_REWARDS_GENERATE
        _WXLWOW_WEEKLY_REWARDS_GENERATE = nil
    end
    if _WXLWOW_WEEKLY_REWARDS_CLAIM_ESSENCE then
        WR.ClaimEssence = _WXLWOW_WEEKLY_REWARDS_CLAIM_ESSENCE
        _WXLWOW_WEEKLY_REWARDS_CLAIM_ESSENCE = nil
    end

    Enum = Enum or {}
    Enum.WeeklyRewardChestThresholdType = Enum.WeeklyRewardChestThresholdType or {
        Raid = 0, Activities = 1, RankedPvP = 2, World = 3, Concession = 4,
    }

    local function HasFlag(value, flag)
        return math.floor((value or 0) / flag) % 2 == 1
    end

    function WR:DrainNative()
        if not self.GetNativeState then return false end
        local values = { self.GetNativeState() }
        if not values[1] then return false end
        local count = tonumber(values[10]) or 0
        local cursor = 11
        local activities = {}
        for i = 1, count do
            local activityFlags = values[cursor + 12] or 0
            activities[i] = {
                type = values[cursor], index = values[cursor + 1],
                threshold = values[cursor + 2], progress = values[cursor + 3],
                level = values[cursor + 4], id = values[cursor + 5],
                activityTierID = values[cursor + 6],
                claimID = values[cursor + 7] ~= 0 and values[cursor + 7] or nil,
                mapID = values[cursor + 8],
                rewardItemID = values[cursor + 9],
                targetItemLevel = values[cursor + 10],
                lootSpecID = values[cursor + 11],
                sourceItemID = values[cursor + 13],
                flags = activityFlags,
                unlocked = HasFlag(activityFlags, 1),
                hasReward = HasFlag(activityFlags, 2),
                claimed = HasFlag(activityFlags, 4),
                rewards = values[cursor + 9] ~= 0 and {
                    { id = values[cursor + 9], itemID = values[cursor + 9] }
                } or {},
            }
            cursor = cursor + 14
        end
        self.state = {
            valid = true, revision = values[2], periodID = values[3],
            periodStart = values[4], periodEnd = values[5], flags = values[6],
            currentLootSpecID = values[7] or 0,
            essenceItemID = values[8] or 0,
            essenceAmount = values[9] or 0,
            isCurrentPeriod = HasFlag(values[6], 1),
            hasAvailableRewards = HasFlag(values[6], 2),
            hasGeneratedRewards = HasFlag(values[6], 4),
            hasInteraction = HasFlag(values[6], 8),
            canClaimRewards = HasFlag(values[6], 16),
            hasClaimed = HasFlag(values[6], 32),
            schemaReady = HasFlag(values[6], 64),
            claimTest = HasFlag(values[6], 128),
            canGenerateRewards = HasFlag(values[6], 2) and
                not HasFlag(values[6], 4) and HasFlag(values[6], 8) and
                not HasFlag(values[6], 32),
            canClaimEssence = (values[8] or 0) > 0 and
                (values[9] or 0) > 0 and HasFlag(values[6], 2) and
                HasFlag(values[6], 8) and not HasFlag(values[6], 32),
            activities = activities,
        }
        return true
    end

    function WR._NativeChanged()
        WR:DrainNative()
        if WR._FrameXMLChanged then WR._FrameXMLChanged() end
    end

    C_WeeklyRewards = C_WeeklyRewards or {}
    function C_WeeklyRewards.GetActivities(activityType)
        local result = {}
        local activities = WR.state and WR.state.activities or {}
        for _, activity in ipairs(activities) do
            if activityType == nil or activity.type == activityType then
                result[#result + 1] = activity
            end
        end
        return result
    end
    function C_WeeklyRewards.AreRewardsForCurrentRewardPeriod()
        return not WR.state or WR.state.isCurrentPeriod
    end
    function C_WeeklyRewards.CanClaimRewards()
        return WR.state and WR.state.canClaimRewards or false
    end
    function C_WeeklyRewards.HasAvailableRewards()
        return WR.state and WR.state.hasAvailableRewards or false
    end
    function C_WeeklyRewards.HasGeneratedRewards()
        return WR.state and WR.state.hasGeneratedRewards or false
    end
    function C_WeeklyRewards.HasInteraction()
        return WR.state and WR.state.hasInteraction or false
    end
    function C_WeeklyRewards.IsWeeklyChestRetired() return false end
    function C_WeeklyRewards.ShouldShowFinalRetirementMessage() return false end
    function C_WeeklyRewards.ShouldShowRetirementMessage() return false end
    function C_WeeklyRewards.GetNumCompletedDungeonRuns()
        local activities = C_WeeklyRewards.GetActivities(Enum.WeeklyRewardChestThresholdType.Activities)
        return 0, 0, activities[1] and activities[1].progress or 0
    end
    function C_WeeklyRewards.GetActivityEncounterInfo() return {} end
    function C_WeeklyRewards.GetSortedProgressForActivity() return {} end
    function C_WeeklyRewards.GetConquestWeeklyProgress()
        return { progress = 0, maxProgress = 0, displayType = 0, unlocksCompleted = 0, maxUnlocks = 0, sampleItemHyperlink = "" }
    end
    function C_WeeklyRewards.GetDifficultyIDForActivityTier() return 0 end
    function C_WeeklyRewards.GetExampleRewardItemHyperlinks() return "", "" end
    function C_WeeklyRewards.GetItemHyperlink(activityID)
        for _, activity in ipairs(WR.state and WR.state.activities or {}) do
            if activity.id == activityID or activity.claimID == activityID then
                if activity.rewardItemID and activity.rewardItemID ~= 0 then
                    return "item:" .. tostring(activity.rewardItemID)
                end
            end
        end
        return nil
    end
    function C_WeeklyRewards.GetNextActivitiesIncrease() return false end
    function C_WeeklyRewards.GetNextMythicPlusIncrease() return false end
    function C_WeeklyRewards.ClaimReward(claimID)
        return WR.ClaimReward and WR.ClaimReward(claimID) or false
    end
    function C_WeeklyRewards.GenerateRewards()
        return WR.GenerateRewards and WR.GenerateRewards() or false
    end
    function C_WeeklyRewards.ClaimEssence()
        return WR.ClaimEssence and WR.ClaimEssence() or false
    end
    function C_WeeklyRewards.CloseInteraction()
        return WR.CloseInteraction and WR.CloseInteraction() or false
    end
    function C_WeeklyRewards.OnUIInteract()
        return WR.OpenInteraction and WR.OpenInteraction() or false
    end

    WR:DrainNative()
    if WR._FrameXMLReady then WR._FrameXMLReady() end
end
)lua";
}

bool wxl_retail_ui::InstallWeeklyRewards()
{
    const WXL_NetworkApi* network = Network();
    const WXL_FrameScriptApi* framescript = FrameScript();
    if (!network || !framescript) return false;

    bool ok = true;
    ok &= network->RegisterClientOpcode(
        opcodes::CmsgWeeklyRewardsRequestState,
        "CMSG_WXL_WEEKLY_REWARDS_REQUEST_STATE") != 0;
    ok &= network->RegisterServerOpcode(
        opcodes::SmsgWeeklyRewardsState,
        "SMSG_WXL_WEEKLY_REWARDS_STATE", &OnState, nullptr) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_REQUEST_STATE", &LuaRequestState) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_GET_STATE", &LuaGetState) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_OPEN", &LuaOpenInteraction) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_CLOSE", &LuaCloseInteraction) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_CLAIM", &LuaClaimReward) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_GENERATE", &LuaGenerateRewards) != 0;
    ok &= framescript->RegisterFunction(
        "_WXLWOW_WEEKLY_REWARDS_CLAIM_ESSENCE", &LuaClaimEssence) != 0;
    ok &= framescript->RegisterScript("weekly-rewards", kBootstrap) != 0;
    if (ok) WLOG_INFO("weekly-rewards: native protocol v4 bridge registered");
    return ok;
}
