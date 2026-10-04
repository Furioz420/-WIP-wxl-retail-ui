// Core-authoritative Retail transmog collection transport and restricted Lua bridge.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"
#include "wxl/Db2Api.h"
#include "wxl/Db2FilterApi.h"
#include "wxl/Db2StringApi.h"
#include "wxl/RetailDb2Api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    namespace luaoff = wxl::offsets::engine::lua;

    namespace opcodes
    {
        constexpr uint16_t CmsgTransmogCollection = 0x053C;
        constexpr uint16_t SmsgTransmogCollection = 0x053D;
        constexpr uint16_t CmsgTransmogApply = 0x053E;
        constexpr uint16_t SmsgTransmogResult = 0x053F;
    }

    constexpr uint8_t kProtocolVersion = 1;
    constexpr uint8_t kCollectionProtocolVersion = 2;
    constexpr uint16_t kMaxPageCount = 280;
    constexpr uint32_t kMaxCollectionCount = 200000;
    constexpr size_t kAppearanceWireBytes = 33;
    constexpr size_t kSourceWireBytes = 46;

    struct Reader
    {
        std::span<const uint8_t> data;
        size_t cursor = 0;

        template <typename T>
        bool Read(T& value)
        {
            if (cursor > data.size() || sizeof(T) > data.size() - cursor)
                return false;
            std::memcpy(&value, data.data() + cursor, sizeof(T));
            cursor += sizeof(T);
            return true;
        }
    };

    struct Appearance
    {
        uint8_t sourceKind = 2; // ItemID=1, exact Retail IMA=2
        uint32_t sourceId = 0;
        uint32_t modifiedAppearanceId = 0;
        uint32_t itemId = 0;
        uint32_t modifierId = 0;
        uint32_t appearanceId = 0;
        uint32_t displayId = 0;
        uint32_t iconFileDataId = 0;
        uint8_t inventoryType = 0;
        uint32_t sourceType = 0;
        uint32_t flags = 0;
        uint32_t classId = 0;
        uint32_t subclassId = 0;
    };

    struct ApplyResult
    {
        uint8_t status = 0;
        int8_t bag = 0;
        uint8_t slot = 0;
        uint32_t modifiedAppearanceId = 0;
        bool valid = false;
    };

    std::mutex g_mutex;
    std::vector<Appearance> g_collection;
    std::unordered_set<uint32_t> g_ownedAppearanceIds;
    uint32_t g_generation = 0;
    bool g_resetOnNextPage = true;
    ApplyResult g_lastResult;

    struct TransmogSet
    {
        uint32_t id = 0;
        std::string name;
        uint32_t trackingQuestId = 0;
        uint32_t flags = 0;
        uint32_t groupId = 0;
        uint32_t parentSetId = 0;
        uint32_t expansionId = 0;
        uint32_t uiOrder = 0;
        std::vector<uint32_t> modifiedAppearanceIds;
        std::vector<uint32_t> fallbackModifiedAppearanceIds;
    };

    std::vector<TransmogSet> g_transmogSets;
    std::unordered_map<uint32_t, size_t> g_transmogSetById;
    std::unordered_map<uint32_t, Appearance> g_staticAppearances;
    std::string g_transmogSetError;
    bool g_transmogSetsLoaded = false;
    bool g_staticAppearancesEnriched = false;
    std::chrono::steady_clock::time_point g_nextStaticEnrichmentAttempt{};

    constexpr WXL_Db2Field kTransmogSetFields[] = {
        { "Name_lang", 1 }, { "ID", 1 }, { "ClassMask", 1 },
        { "TrackingQuestID", 1 }, { "Flags", 1 }, { "TransmogSetGroupID", 1 },
        { "ItemNameDescriptionID", 1 }, { "ParentTransmogSetID", 1 },
        { "CompleteWorldStateID", 1 }, { "ExpansionID", 1 },
        { "PatchIntroduced", 1 }, { "UiOrder", 1 }, { "ConditionID", 1 },
    };

    constexpr WXL_Db2Definition kTransmogSetDefinition = {
        "TransmogSet", "transmogset.db2", 0xC6875C71u,
        kTransmogSetFields, static_cast<uint32_t>(std::size(kTransmogSetFields)),
        nullptr, 0, 1,
    };

    constexpr WXL_Db2Field kTransmogSetItemFields[] = {
        { "TransmogSetID", 1 }, { "ItemModifiedAppearanceID", 1 },
        { "Flags", 1 },
    };

    constexpr WXL_Db2Definition kTransmogSetItemDefinition = {
        "TransmogSetItem", "transmogsetitem.db2", 0xE6EFF061u,
        kTransmogSetItemFields,
        static_cast<uint32_t>(std::size(kTransmogSetItemFields)),
        nullptr, 0, 1,
    };

    constexpr WXL_Db2Field kItemModifiedAppearanceFields[] = {
        { "ID", 1 }, { "ItemID", 1 }, { "ItemAppearanceModifierID", 1 },
        { "ItemAppearanceID", 1 }, { "OrderIndex", 1 },
        { "TransmogSourceTypeEnum", 1 }, { "Flags", 1 },
    };

    constexpr WXL_Db2Definition kItemModifiedAppearanceDefinition = {
        "ItemModifiedAppearance", "itemmodifiedappearance.db2", 0x03A6C979u,
        kItemModifiedAppearanceFields,
        static_cast<uint32_t>(std::size(kItemModifiedAppearanceFields)),
        nullptr, 0, 1,
    };

    bool EnrichStaticAppearancesFromRetailCatalog()
    {
        const auto* retail = static_cast<const WXL_RetailDb2Api*>(
            wxl_retail_ui::g_api->GetInterface(
                "wxl.retail-db2", WXL_RETAIL_DB2_API_VERSION));
        if (!retail || !retail->Enabled || !retail->Enabled() ||
            !retail->AcquireCatalog || !retail->ReleaseCatalog)
        {
            WLOG_WARN("retail-transmog: retail item catalog unavailable; set members retain DB2 IDs only");
            return false;
        }

        void* catalog = retail->AcquireCatalog();
        if (!catalog)
        {
            WLOG_WARN("retail-transmog: retail item catalog has not published a snapshot; set members retain DB2 IDs only");
            return false;
        }

        std::unordered_map<uint64_t, uint32_t> appearanceByVariant;
        std::unordered_map<uint32_t, std::vector<uint32_t>> appearancesByItem;
        appearanceByVariant.reserve(g_staticAppearances.size());
        appearancesByItem.reserve(g_staticAppearances.size());
        for (const auto& [modifiedAppearanceId, appearance] : g_staticAppearances)
        {
            const uint64_t key = (static_cast<uint64_t>(appearance.itemId) << 32) |
                appearance.modifierId;
            appearanceByVariant.try_emplace(key, modifiedAppearanceId);
            appearancesByItem[appearance.itemId].push_back(modifiedAppearanceId);
        }

        if (retail->CatalogVariantCount && retail->CatalogVariantAt)
        {
            const uint32_t count = retail->CatalogVariantCount(catalog);
            for (uint32_t index = 0; index < count; ++index)
            {
                uint64_t key = 0;
                WXL_RetailItemVariant variant{};
                if (!retail->CatalogVariantAt(catalog, index, &key, &variant))
                    continue;
                const auto found = appearanceByVariant.find(key);
                if (found == appearanceByVariant.end())
                    continue;
                Appearance& appearance = g_staticAppearances[found->second];
                appearance.displayId = variant.displayId;
                appearance.appearanceId = variant.appearanceId;
                appearance.iconFileDataId = variant.iconFileDataId;
            }
        }

        if (retail->CatalogItemCount && retail->CatalogItemAt)
        {
            const uint32_t count = retail->CatalogItemCount(catalog);
            for (uint32_t index = 0; index < count; ++index)
            {
                uint32_t itemId = 0;
                WXL_RetailItemInfo item{};
                if (!retail->CatalogItemAt(catalog, index, &itemId, &item))
                    continue;
                const auto found = appearancesByItem.find(itemId);
                if (found == appearancesByItem.end())
                    continue;
                for (const uint32_t modifiedAppearanceId : found->second)
                {
                    Appearance& appearance = g_staticAppearances[modifiedAppearanceId];
                    appearance.inventoryType = static_cast<uint8_t>(item.inventoryType);
                    appearance.classId = item.classId;
                    appearance.subclassId = item.subclassId;
                }
            }
        }

        retail->ReleaseCatalog(catalog);
        WLOG_INFO("retail-transmog: enriched %zu static set appearances from retail item catalog",
            g_staticAppearances.size());
        return true;
    }

    bool EnsureStaticAppearancesEnriched()
    {
        if (g_staticAppearancesEnriched) return true;
        const auto now = std::chrono::steady_clock::now();
        if (now < g_nextStaticEnrichmentAttempt) return false;
        g_nextStaticEnrichmentAttempt = now + std::chrono::seconds(1);
        g_staticAppearancesEnriched =
            EnrichStaticAppearancesFromRetailCatalog();
        return g_staticAppearancesEnriched;
    }

    void EnrichOwnedAppearancesFromRetailCatalog()
    {
        const auto* retail = static_cast<const WXL_RetailDb2Api*>(
            wxl_retail_ui::g_api->GetInterface(
                "wxl.retail-db2", WXL_RETAIL_DB2_API_VERSION));
        if (!retail || !retail->Enabled || !retail->Enabled() ||
            !retail->AcquireCatalog || !retail->ReleaseCatalog ||
            !retail->CatalogItemCount || !retail->CatalogItemAt)
            return;

        void* catalog = retail->AcquireCatalog();
        if (!catalog)
            return;

        std::unordered_set<uint32_t> wantedItems;
        {
            const std::lock_guard lock(g_mutex);
            wantedItems.reserve(g_collection.size());
            for (const Appearance& appearance : g_collection)
                wantedItems.insert(appearance.itemId);
        }

        std::unordered_map<uint32_t, WXL_RetailItemInfo> itemInfo;
        itemInfo.reserve(wantedItems.size());
        const uint32_t count = retail->CatalogItemCount(catalog);
        for (uint32_t index = 0; index < count; ++index)
        {
            uint32_t itemId = 0;
            WXL_RetailItemInfo item{};
            if (retail->CatalogItemAt(catalog, index, &itemId, &item) &&
                wantedItems.contains(itemId))
                itemInfo.try_emplace(itemId, item);
        }
        retail->ReleaseCatalog(catalog);

        {
            const std::lock_guard lock(g_mutex);
            for (Appearance& appearance : g_collection)
            {
                const auto found = itemInfo.find(appearance.itemId);
                if (found == itemInfo.end())
                    continue;
                appearance.classId = found->second.classId;
                appearance.subclassId = found->second.subclassId;
                if (!appearance.inventoryType)
                    appearance.inventoryType =
                        static_cast<uint8_t>(found->second.inventoryType);
            }
        }
        WLOG_INFO("retail-transmog: enriched %zu owned item classifications",
            itemInfo.size());
    }

    bool LoadTransmogSetCatalog()
    {
        const auto* db2 = static_cast<const WXL_Db2Api*>(
            wxl_retail_ui::g_api->GetInterface("wxl.db2", WXL_DB2_API_VERSION));
        const auto* strings = static_cast<const WXL_Db2StringApi*>(
            wxl_retail_ui::g_api->GetInterface(
                "wxl.db2.strings", WXL_DB2_STRING_API_VERSION));
        const auto* filter = static_cast<const WXL_Db2FilterApi*>(
            wxl_retail_ui::g_api->GetInterface(
                "wxl.db2.filtered", WXL_DB2_FILTER_API_VERSION));
        if (!db2 || !strings)
        {
            g_transmogSetError = "wxl.db2 and wxl.db2.strings are required";
            return false;
        }

        char error[512] = {};
        void* setTable = db2->Load(&kTransmogSetDefinition, error, sizeof error);
        if (!setTable)
        {
            g_transmogSetError = error[0] ? error : "TransmogSet.db2 failed to load";
            return false;
        }
        void* itemTable = db2->Load(&kTransmogSetItemDefinition, error, sizeof error);
        if (!itemTable)
        {
            g_transmogSetError = error[0] ? error : "TransmogSetItem.db2 failed to load";
            db2->Release(setTable);
            return false;
        }

        g_transmogSets.clear();
        g_transmogSetById.clear();
        g_transmogSets.reserve(db2->RowCount(setTable));
        for (uint32_t index = 0; index < db2->RowCount(setTable); ++index)
        {
            const void* row = db2->RowAt(setTable, index);
            if (!row)
                continue;
            uint32_t id = db2->Value(setTable, row, "ID", 0);
            if (!id)
                id = db2->RowId(row);
            if (!id)
                continue;

            TransmogSet record;
            record.id = id;
            if (const char* name = strings->String(setTable, row, "Name_lang", 0))
                record.name = name;
            record.trackingQuestId = db2->Value(setTable, row, "TrackingQuestID", 0);
            record.flags = db2->Value(setTable, row, "Flags", 0);
            record.groupId = db2->Value(setTable, row, "TransmogSetGroupID", 0);
            record.parentSetId = db2->Value(setTable, row, "ParentTransmogSetID", 0);
            record.expansionId = db2->Value(setTable, row, "ExpansionID", 0);
            record.uiOrder = db2->Value(setTable, row, "UiOrder", 0);
            g_transmogSetById.try_emplace(id, g_transmogSets.size());
            g_transmogSets.push_back(std::move(record));
        }

        for (uint32_t index = 0; index < db2->RowCount(itemTable); ++index)
        {
            const void* row = db2->RowAt(itemTable, index);
            if (!row)
                continue;
            const uint32_t setId = db2->Value(itemTable, row, "TransmogSetID", 0);
            const uint32_t modifiedAppearanceId =
                db2->Value(itemTable, row, "ItemModifiedAppearanceID", 0);
            const uint32_t flags = db2->Value(itemTable, row, "Flags", 0);
            const auto found = g_transmogSetById.find(setId);
            if (found != g_transmogSetById.end() && modifiedAppearanceId)
            {
                TransmogSet& set = g_transmogSets[found->second];
                set.fallbackModifiedAppearanceIds.push_back(modifiedAppearanceId);
                // Retail's wardrobe uses GetSetPrimaryAppearances for set
                // progress and presentation. Bit 0 marks those primary rows;
                // the remaining rows are alternate/extended sources that the
                // ensemble may still grant, but should not appear as 19 equal
                // pieces in the wardrobe set view.
                if ((flags & 1u) != 0)
                    set.modifiedAppearanceIds.push_back(modifiedAppearanceId);
            }
        }

        // A small number of legacy/test sets have no primary marker. Retain a
        // useful catalog for those records rather than displaying an empty set.
        for (TransmogSet& set : g_transmogSets)
        {
            if (set.modifiedAppearanceIds.empty())
                set.modifiedAppearanceIds = std::move(set.fallbackModifiedAppearanceIds);
            else
                set.fallbackModifiedAppearanceIds.clear();
        }

        db2->Release(itemTable);
        db2->Release(setTable);

        std::unordered_set<uint32_t> setAppearanceIds;
        for (const TransmogSet& set : g_transmogSets)
            setAppearanceIds.insert(
                set.modifiedAppearanceIds.begin(), set.modifiedAppearanceIds.end());

        std::vector<uint32_t> appearanceFilterValues(
            setAppearanceIds.begin(), setAppearanceIds.end());
        const WXL_Db2Filter appearanceFilter{
            WXL_DB2_FILTER_ROW_ID,
            nullptr,
            0,
            appearanceFilterValues.data(),
            static_cast<uint32_t>(appearanceFilterValues.size()),
        };

        constexpr size_t kMaxFilteredAppearanceIds = 4096;
        void* appearanceTable = nullptr;
        if (filter && filter->LoadFiltered &&
            appearanceFilterValues.size() <= kMaxFilteredAppearanceIds)
        {
            appearanceTable = filter->LoadFiltered(
                &kItemModifiedAppearanceDefinition, &appearanceFilter, error, sizeof error);
            if (!appearanceTable && error[0])
                WLOG_WARN("retail-transmog: filtered ItemModifiedAppearance load failed (%s); retrying unfiltered",
                    error);
        }
        if (!appearanceTable)
        {
            error[0] = '\0';
            appearanceTable = db2->Load(
                &kItemModifiedAppearanceDefinition, error, sizeof error);
        }
        if (!appearanceTable)
        {
            g_transmogSetError = error[0]
                ? error : "ItemModifiedAppearance.db2 failed to load";
            return false;
        }

        g_staticAppearances.clear();
        g_staticAppearancesEnriched = false;
        g_nextStaticEnrichmentAttempt = {};
        g_staticAppearances.reserve(setAppearanceIds.size());
        for (uint32_t index = 0; index < db2->RowCount(appearanceTable); ++index)
        {
            const void* row = db2->RowAt(appearanceTable, index);
            if (!row)
                continue;
            uint32_t modifiedAppearanceId =
                db2->Value(appearanceTable, row, "ID", 0);
            if (!modifiedAppearanceId)
                modifiedAppearanceId = db2->RowId(row);
            if (!setAppearanceIds.contains(modifiedAppearanceId))
                continue;

            Appearance appearance;
            appearance.modifiedAppearanceId = modifiedAppearanceId;
            appearance.itemId = db2->Value(appearanceTable, row, "ItemID", 0);
            appearance.modifierId = db2->Value(
                appearanceTable, row, "ItemAppearanceModifierID", 0);
            appearance.appearanceId = db2->Value(
                appearanceTable, row, "ItemAppearanceID", 0);
            appearance.sourceType = db2->Value(
                appearanceTable, row, "TransmogSourceTypeEnum", 0);
            appearance.flags = db2->Value(appearanceTable, row, "Flags", 0);
            g_staticAppearances.try_emplace(modifiedAppearanceId, appearance);
        }
        db2->Release(appearanceTable);
        EnsureStaticAppearancesEnriched();

        for (TransmogSet& set : g_transmogSets)
        {
            std::sort(set.modifiedAppearanceIds.begin(), set.modifiedAppearanceIds.end());
            set.modifiedAppearanceIds.erase(
                std::unique(set.modifiedAppearanceIds.begin(), set.modifiedAppearanceIds.end()),
                set.modifiedAppearanceIds.end());
        }
        std::erase_if(g_transmogSets, [](const TransmogSet& set)
        {
            return set.modifiedAppearanceIds.empty();
        });
        std::sort(g_transmogSets.begin(), g_transmogSets.end(),
            [](const TransmogSet& left, const TransmogSet& right)
            {
                return left.id < right.id;
            });
        g_transmogSetById.clear();
        for (size_t index = 0; index < g_transmogSets.size(); ++index)
            g_transmogSetById.emplace(g_transmogSets[index].id, index);

        g_transmogSetsLoaded = true;
        g_transmogSetError.clear();
        WLOG_INFO("retail-transmog: loaded %zu TransmogSet records and %zu static appearances for collection UI",
            g_transmogSets.size(), g_staticAppearances.size());
        return true;
    }

    template <typename T>
    void Append(std::vector<uint8_t>& bytes, T value)
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + sizeof(T));
        std::memcpy(bytes.data() + offset, &value, sizeof(T));
    }

    bool SendCollectionRequest(uint32_t cursor)
    {
        std::vector<uint8_t> payload;
        payload.reserve(5);
        Append(payload, kCollectionProtocolVersion);
        Append(payload, cursor);
        return wxl_retail_ui::Network()->Send(opcodes::CmsgTransmogCollection,
            payload.data(), static_cast<uint32_t>(payload.size())) != 0;
    }

    void __cdecl OnCollection(const uint8_t* data, uint32_t size, void*)
    {
        const std::span<const uint8_t> payload(data, size);
        Reader reader{payload};
        uint8_t version = 0;
        uint32_t total = 0;
        uint32_t nextCursor = 0;
        uint16_t count = 0;
        if (!reader.Read(version) || (version != kProtocolVersion && version != kCollectionProtocolVersion) ||
            !reader.Read(total) || total > kMaxCollectionCount ||
            !reader.Read(nextCursor) || !reader.Read(count) ||
            count > kMaxPageCount || payload.size() - reader.cursor != size_t(count) *
                (version == kCollectionProtocolVersion ? kSourceWireBytes : kAppearanceWireBytes))
        {
            WLOG_WARN("retail-transmog: rejected malformed collection page bytes=%u", size);
            return;
        }

        std::vector<Appearance> page;
        page.reserve(count);
        for (uint16_t index = 0; index < count; ++index)
        {
            Appearance appearance;
            if (version == kCollectionProtocolVersion &&
                (!reader.Read(appearance.sourceKind) || !reader.Read(appearance.sourceId)))
                return;
            if (!reader.Read(appearance.modifiedAppearanceId) ||
                !reader.Read(appearance.itemId) || !reader.Read(appearance.modifierId) ||
                !reader.Read(appearance.appearanceId) || !reader.Read(appearance.displayId) ||
                !reader.Read(appearance.iconFileDataId) || !reader.Read(appearance.inventoryType) ||
                !reader.Read(appearance.sourceType) || !reader.Read(appearance.flags))
                return;
            if (version == kCollectionProtocolVersion)
            {
                if (!reader.Read(appearance.classId) || !reader.Read(appearance.subclassId) ||
                    !appearance.sourceId ||
                    (appearance.sourceKind != 1 && appearance.sourceKind != 2) ||
                    (appearance.sourceKind == 1 && (appearance.sourceId != appearance.itemId ||
                        appearance.modifiedAppearanceId || appearance.modifierId)) ||
                    (appearance.sourceKind == 2 && appearance.sourceId != appearance.modifiedAppearanceId))
                    return;
            }
            else
                appearance.sourceId = appearance.modifiedAppearanceId;
            page.push_back(appearance);
        }

        bool complete = false;
        {
            const std::lock_guard lock(g_mutex);
            if (g_resetOnNextPage)
            {
                g_collection.clear();
                g_collection.reserve(total);
                g_resetOnNextPage = false;
            }
            g_collection.insert(g_collection.end(), page.begin(), page.end());
            if (!nextCursor)
            {
                g_ownedAppearanceIds.clear();
                g_ownedAppearanceIds.reserve(g_collection.size());
                for (const Appearance& appearance : g_collection)
                    if (appearance.sourceKind == 2)
                        g_ownedAppearanceIds.insert(appearance.modifiedAppearanceId);
                ++g_generation;
                g_resetOnNextPage = true;
                complete = true;
                WLOG_INFO("retail-transmog: received %zu of %u owned appearances",
                    g_collection.size(), total);
            }
        }
        if (complete && version == kProtocolVersion)
            EnrichOwnedAppearancesFromRetailCatalog();
        if (nextCursor)
            SendCollectionRequest(nextCursor);
    }

    void __cdecl OnApplyResult(const uint8_t* data, uint32_t size, void*)
    {
        if (size != 8)
            return;
        Reader reader{std::span<const uint8_t>(data, size)};
        uint8_t version = 0;
        ApplyResult result;
        if (!reader.Read(version) || version != kProtocolVersion ||
            !reader.Read(result.status) || !reader.Read(result.bag) ||
            !reader.Read(result.slot) || !reader.Read(result.modifiedAppearanceId))
            return;
        result.valid = true;
        const std::lock_guard lock(g_mutex);
        g_lastResult = result;
        ++g_generation;
    }

    int __cdecl LuaRequestCollection(void* state)
    {
        {
            const std::lock_guard lock(g_mutex);
            g_resetOnNextPage = true;
        }
        wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(
            state, SendCollectionRequest(0) ? 1 : 0);
        return 1;
    }

    int __cdecl LuaCollectionCount(void* state)
    {
        const std::lock_guard lock(g_mutex);
        wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber)(
            state, static_cast<double>(g_collection.size()));
        return 1;
    }

    int __cdecl LuaCollectionGeneration(void* state)
    {
        const std::lock_guard lock(g_mutex);
        wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber)(state, g_generation);
        return 1;
    }

    int __cdecl LuaCollectionAppearance(void* state)
    {
        const double requested =
            wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber)(state, 1);
        if (!std::isfinite(requested) || requested < 1.0)
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }

        Appearance appearance;
        {
            const std::lock_guard lock(g_mutex);
            const size_t index = static_cast<size_t>(requested - 1.0);
            if (index >= g_collection.size())
            {
                wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
                return 1;
            }
            appearance = g_collection[index];
        }

        const auto pushNumber =
            wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber);
        pushNumber(state, appearance.modifiedAppearanceId);
        pushNumber(state, appearance.itemId);
        pushNumber(state, appearance.modifierId);
        pushNumber(state, appearance.appearanceId);
        pushNumber(state, appearance.displayId);
        const WXL_FdidApi* fdid = wxl_retail_ui::Fdid();
        const char* icon = fdid && fdid->ResolveTexture
            ? fdid->ResolveTexture(appearance.iconFileDataId) : nullptr;
        if (!icon || !*icon)
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
        else
            wxl::game::Native<luaoff::LuaPushStringFn>(luaoff::kLuaPushString)(state, icon);
        pushNumber(state, appearance.inventoryType);
        pushNumber(state, appearance.sourceType);
        pushNumber(state, appearance.flags);
        pushNumber(state, appearance.classId);
        pushNumber(state, appearance.subclassId);
        pushNumber(state, appearance.sourceKind);
        pushNumber(state, appearance.sourceId);
        return 13;
    }

    int __cdecl LuaTransmogSetCount(void* state)
    {
        wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber)(
            state, static_cast<double>(g_transmogSets.size()));
        return 1;
    }

    int __cdecl LuaTransmogSetInfo(void* state)
    {
        EnsureStaticAppearancesEnriched();
        const double requested =
            wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber)(state, 1);
        if (!std::isfinite(requested) || requested < 1.0)
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }
        const size_t index = static_cast<size_t>(requested - 1.0);
        if (index >= g_transmogSets.size())
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }

        const TransmogSet& set = g_transmogSets[index];
        uint32_t owned = 0;
        {
            const std::lock_guard lock(g_mutex);
            for (uint32_t modifiedAppearanceId : set.modifiedAppearanceIds)
                if (g_ownedAppearanceIds.contains(modifiedAppearanceId))
                    ++owned;
        }

        const auto pushNumber =
            wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber);
        pushNumber(state, set.id);
        wxl::game::Native<luaoff::LuaPushStringFn>(luaoff::kLuaPushString)(
            state, set.name.empty() ? "Unnamed set" : set.name.c_str());
        pushNumber(state, owned);
        pushNumber(state, static_cast<double>(set.modifiedAppearanceIds.size()));
        wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(
            state, owned == set.modifiedAppearanceIds.size() ? 1 : 0);
        pushNumber(state, set.trackingQuestId);
        pushNumber(state, set.parentSetId);
        pushNumber(state, set.groupId);
        pushNumber(state, set.flags);
        pushNumber(state, set.expansionId);
        pushNumber(state, set.uiOrder);
        uint32_t armorMask = 0;
        for (uint32_t id : set.modifiedAppearanceIds)
        {
            const auto found = g_staticAppearances.find(id);
            if (found == g_staticAppearances.end()) continue;
            const auto& a = found->second;
            // Cloaks/shirts do not determine whether a set is cloth or plate.
            if (a.classId == 4 && a.subclassId >= 1 && a.subclassId <= 4 &&
                (a.inventoryType == 1 || a.inventoryType == 3 || a.inventoryType == 5 ||
                 a.inventoryType == 20 || (a.inventoryType >= 6 && a.inventoryType <= 10)))
                armorMask |= 1u << (a.subclassId - 1);
        }
        pushNumber(state, armorMask);
        return 12;
    }

    int __cdecl LuaTransmogSetMember(void* state)
    {
        EnsureStaticAppearancesEnriched();
        const auto toNumber = wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber);
        const double requestedSet = toNumber(state, 1);
        const double requestedMember = toNumber(state, 2);
        if (!std::isfinite(requestedSet) || requestedSet < 1.0 ||
            !std::isfinite(requestedMember) || requestedMember < 1.0)
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }

        const size_t setIndex = static_cast<size_t>(requestedSet - 1.0);
        const size_t memberIndex = static_cast<size_t>(requestedMember - 1.0);
        if (setIndex >= g_transmogSets.size() ||
            memberIndex >= g_transmogSets[setIndex].modifiedAppearanceIds.size())
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }

        const uint32_t modifiedAppearanceId =
            g_transmogSets[setIndex].modifiedAppearanceIds[memberIndex];
        Appearance appearance;
        if (const auto found = g_staticAppearances.find(modifiedAppearanceId);
            found != g_staticAppearances.end())
            appearance = found->second;
        bool owned = false;
        {
            const std::lock_guard lock(g_mutex);
            const auto found = std::find_if(g_collection.begin(), g_collection.end(),
                [modifiedAppearanceId](const Appearance& candidate)
                {
                    return candidate.modifiedAppearanceId == modifiedAppearanceId;
                });
            if (found != g_collection.end())
            {
                appearance = *found;
                owned = true;
            }
        }

        const auto pushNumber =
            wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber);
        pushNumber(state, modifiedAppearanceId);
        wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(state, owned ? 1 : 0);
        pushNumber(state, appearance.itemId);
        pushNumber(state, appearance.modifierId);
        pushNumber(state, appearance.appearanceId);
        pushNumber(state, appearance.displayId);
        const WXL_FdidApi* fdid = wxl_retail_ui::Fdid();
        const char* icon = fdid && fdid->ResolveTexture
            ? fdid->ResolveTexture(appearance.iconFileDataId) : nullptr;
        if (!icon || !*icon)
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
        else
            wxl::game::Native<luaoff::LuaPushStringFn>(luaoff::kLuaPushString)(state, icon);
        pushNumber(state, appearance.inventoryType);
        pushNumber(state, appearance.sourceType);
        pushNumber(state, appearance.flags);
        return 10;
    }

    int __cdecl LuaTransmogSetCatalogStatus(void* state)
    {
        EnsureStaticAppearancesEnriched();
        wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(
            state, g_transmogSetsLoaded ? 1 : 0);
        wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber)(
            state, static_cast<double>(g_transmogSets.size()));
        if (g_transmogSetError.empty())
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
        else
            wxl::game::Native<luaoff::LuaPushStringFn>(luaoff::kLuaPushString)(
                state, g_transmogSetError.c_str());
        return 3;
    }

    int __cdecl LuaApply(void* state)
    {
        const auto toNumber = wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber);
        const double bagValue = toNumber(state, 1);
        const double slotValue = toNumber(state, 2);
        const double appearanceValue = toNumber(state, 3);
        bool sent = false;
        if (std::isfinite(bagValue) && bagValue >= -128.0 && bagValue <= 127.0 &&
            std::isfinite(slotValue) && slotValue >= 0.0 && slotValue <= 255.0 &&
            std::isfinite(appearanceValue) && appearanceValue >= 0.0 &&
            appearanceValue <= 4294967295.0)
        {
            std::vector<uint8_t> payload;
            payload.reserve(7);
            Append(payload, kProtocolVersion);
            Append(payload, static_cast<int8_t>(bagValue));
            Append(payload, static_cast<uint8_t>(slotValue));
            Append(payload, static_cast<uint32_t>(appearanceValue));
            sent = wxl_retail_ui::Network()->Send(opcodes::CmsgTransmogApply,
                payload.data(), static_cast<uint32_t>(payload.size())) != 0;
        }
        wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(state, sent ? 1 : 0);
        return 1;
    }

    int __cdecl LuaLastResult(void* state)
    {
        ApplyResult result;
        {
            const std::lock_guard lock(g_mutex);
            result = g_lastResult;
        }
        if (!result.valid)
        {
            wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
            return 1;
        }
        const auto pushNumber =
            wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber);
        pushNumber(state, result.status);
        pushNumber(state, result.bag);
        pushNumber(state, result.slot);
        pushNumber(state, result.modifiedAppearanceId);
        return 4;
    }

    constexpr const char* kBootstrap = R"lua(
wxlwow = wxlwow or {}
wxlwow.request_transmog_collection = _WXLWOW_REQUEST_TRANSMOG_COLLECTION
wxlwow.transmog_collection_count = _WXLWOW_TRANSMOG_COLLECTION_COUNT
wxlwow.transmog_collection_generation = _WXLWOW_TRANSMOG_COLLECTION_GENERATION
wxlwow.transmog_collection_appearance = _WXLWOW_TRANSMOG_COLLECTION_APPEARANCE
wxlwow.transmog_set_count = _WXLWOW_TRANSMOG_SET_COUNT
wxlwow.transmog_set_info = _WXLWOW_TRANSMOG_SET_INFO
wxlwow.transmog_set_member = _WXLWOW_TRANSMOG_SET_MEMBER
wxlwow.transmog_set_catalog_status = _WXLWOW_TRANSMOG_SET_CATALOG_STATUS
wxlwow.apply_transmog = _WXLWOW_APPLY_TRANSMOG
wxlwow.transmog_result = _WXLWOW_TRANSMOG_RESULT
_WXLWOW_REQUEST_TRANSMOG_COLLECTION = nil
_WXLWOW_TRANSMOG_COLLECTION_COUNT = nil
_WXLWOW_TRANSMOG_COLLECTION_GENERATION = nil
_WXLWOW_TRANSMOG_COLLECTION_APPEARANCE = nil
_WXLWOW_TRANSMOG_SET_COUNT = nil
_WXLWOW_TRANSMOG_SET_INFO = nil
_WXLWOW_TRANSMOG_SET_MEMBER = nil
_WXLWOW_TRANSMOG_SET_CATALOG_STATUS = nil
_WXLWOW_APPLY_TRANSMOG = nil
_WXLWOW_TRANSMOG_RESULT = nil
)lua";

    constexpr const char* kCollectionUiBootstrap = R"lua(
do
    local UI = { installed = false, mode = "items", page = 1, pageSize = 12,
        generation = -1, elapsed = 0, sets = nil, filteredSets = nil }

    local function HideLegacyContent()
        local names = {
            "WardrobeCollectionFrameItemsCollectionFrame",
            "WardrobeCollectionFrameSetsCollectionFrame",
            "WardrobeCollectionFrameSetsTransmogFrame",
        }
        for _, name in ipairs(names) do
            local frame = _G[name]
            if frame then frame:Hide() end
        end
    end

    local function SetRowVisible(row, visible)
        if visible then row:Show() else row:Hide() end
    end

    local function BuildSetCache()
        UI.sets = {}
        if not wxlwow or not wxlwow.transmog_set_count or not wxlwow.transmog_set_info then
            return
        end
        local count = wxlwow.transmog_set_count() or 0
        for index = 1, count do
            local id, name, owned, total, complete, quest, parent, group, flags, expansion, order =
                wxlwow.transmog_set_info(index)
            if id then
                table.insert(UI.sets, { id = id, name = name or ("Set " .. id),
                    owned = owned or 0, total = total or 0, complete = complete,
                    quest = quest or 0, parent = parent or 0, group = group or 0,
                    flags = flags or 0, expansion = expansion or 0, order = order or 0 })
            end
        end
        table.sort(UI.sets, function(a, b)
            if a.complete ~= b.complete then return a.complete end
            local ap = a.owned > 0
            local bp = b.owned > 0
            if ap ~= bp then return ap end
            if a.owned ~= b.owned then return a.owned > b.owned end
            if a.name ~= b.name then return a.name < b.name end
            return a.id < b.id
        end)
    end

    local function FilterSets()
        UI.filteredSets = {}
        local query = ""
        if UI.search then query = string.lower(UI.search:GetText() or "") end
        for _, set in ipairs(UI.sets or {}) do
            if query == "" or string.find(string.lower(set.name), query, 1, true)
                or string.find(tostring(set.id), query, 1, true) then
                table.insert(UI.filteredSets, set)
            end
        end
    end

    local function Refresh()
        if not UI.installed or not UI.body:IsShown() then return end
        HideLegacyContent()
        local generation = wxlwow.transmog_collection_generation and
            wxlwow.transmog_collection_generation() or 0
        if generation ~= UI.generation then
            UI.generation = generation
            BuildSetCache()
            UI.page = 1
        end

        for _, row in ipairs(UI.rows) do SetRowVisible(row, false) end
        local total = 0
        if UI.mode == "items" then
            total = wxlwow.transmog_collection_count and wxlwow.transmog_collection_count() or 0
            local first = (UI.page - 1) * UI.pageSize + 1
            for slot = 1, UI.pageSize do
                local index = first + slot - 1
                if index <= total then
                    local ima, item, modifier, appearance, display, icon, inventoryType =
                        wxlwow.transmog_collection_appearance(index)
                    local row = UI.rows[slot]
                    local itemName = item and GetItemInfo(item)
                    row.title:SetText(itemName or ("Retail item " .. tostring(item or 0)))
                    row.detail:SetText(string.format("IMA %d   Item %d   Modifier %d   Inventory %d",
                        ima or 0, item or 0, modifier or 0, inventoryType or 0))
                    row.icon:SetTexture(icon or (item and GetItemIcon(item)) or
                        "Interface\\Icons\\INV_Misc_QuestionMark")
                    row.ima, row.item, row.modifier = ima, item, modifier
                    row.title:SetTextColor(1, .82, 0)
                    SetRowVisible(row, true)
                end
            end
            UI.summary:SetText(string.format("Owned appearances: %d", total))
        else
            FilterSets()
            total = #(UI.filteredSets or {})
            local first = (UI.page - 1) * UI.pageSize + 1
            for slot = 1, UI.pageSize do
                local set = UI.filteredSets[first + slot - 1]
                if set then
                    local row = UI.rows[slot]
                    row.icon:SetTexture(set.complete and "Interface\\Icons\\Achievement_General" or
                        "Interface\\Icons\\INV_Misc_QuestionMark")
                    row.title:SetText(set.name)
                    row.detail:SetText(string.format("TransmogSet %d   %d / %d appearances%s",
                        set.id, set.owned, set.total, set.complete and "   Collected" or ""))
                    if set.complete then
                        row.title:SetTextColor(.2, 1, .2)
                    elseif set.owned > 0 then
                        row.title:SetTextColor(1, .82, 0)
                    else
                        row.title:SetTextColor(.65, .65, .65)
                    end
                    row.set = set
                    SetRowVisible(row, true)
                end
            end
            local loaded, setCount, err = wxlwow.transmog_set_catalog_status()
            if loaded then
                UI.summary:SetText(string.format("Retail sets: %d   Showing: %d", setCount or 0, total))
            else
                UI.summary:SetText("Retail set catalog unavailable: " .. tostring(err or "unknown error"))
            end
        end

        local pages = math.max(1, math.ceil(total / UI.pageSize))
        if UI.page > pages then UI.page = pages end
        UI.pageText:SetText(string.format("Page %d / %d", UI.page, pages))
        if UI.page > 1 then UI.prev:Enable() else UI.prev:Disable() end
        if UI.page < pages then UI.next:Enable() else UI.next:Disable() end
    end

    local function ShowMode(mode)
        UI.mode = mode
        UI.page = 1
        if UI.search then UI.search:SetText("") end
        PanelTemplates_SetTab(WardrobeCollectionFrame, mode == "items" and 1 or 2)
        Refresh()
    end

    local function TryInstall()
        if UI.installed then return true end
        if not CollectionsJournalTab5 or not CollectionsJournalTab6 or
            not WardrobeCollectionFrame or not WardrobeCollectionFrameTab1 or
            not WardrobeCollectionFrameTab2 then return false end

        CollectionsJournalTab5:SetText(WARDROBE or "Appearances")
        CollectionsJournalTab5:Show()
        CollectionsJournalTab6:ClearAllPoints()
        CollectionsJournalTab6:SetPoint("LEFT", CollectionsJournalTab5, "RIGHT", -16, 0)
        WardrobeCollectionFrameTab1:SetText("Items")
        WardrobeCollectionFrameTab2:SetText("Sets")
        WardrobeCollectionFrameTab2:Show()

        local body = CreateFrame("Frame", "WXLTransmogCollectionBody", WardrobeCollectionFrame)
        body:SetPoint("TOPLEFT", 12, -62)
        body:SetPoint("BOTTOMRIGHT", -12, 12)
        body:SetFrameLevel(WardrobeCollectionFrame:GetFrameLevel() + 20)
        body:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true, tileSize = 16,
            edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
        body:SetBackdropColor(.04, .04, .04, .94)
        UI.body = body

        UI.summary = body:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        UI.summary:SetPoint("TOPLEFT", 12, -12)

        UI.search = CreateFrame("EditBox", "WXLTransmogSetSearch", body, "InputBoxTemplate")
        UI.search:SetSize(190, 22)
        UI.search:SetPoint("TOPRIGHT", -12, -8)
        UI.search:SetAutoFocus(false)
        UI.search:SetText("")
        UI.search:SetScript("OnTextChanged", function() if UI.mode == "sets" then UI.page = 1; Refresh() end end)
        UI.search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)

        UI.rows = {}
        for index = 1, UI.pageSize do
            local row = CreateFrame("Button", "WXLTransmogCollectionRow" .. index, body)
            row:SetHeight(38)
            row:SetPoint("TOPLEFT", 12, -38 - (index - 1) * 39)
            row:SetPoint("TOPRIGHT", -12, -38 - (index - 1) * 39)
            row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
            row.icon = row:CreateTexture(nil, "ARTWORK")
            row.icon:SetSize(32, 32)
            row.icon:SetPoint("LEFT", 2, 0)
            row.title = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
            row.title:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, -1)
            row.title:SetPoint("RIGHT", -4, 0)
            row.title:SetJustifyH("LEFT")
            row.detail = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            row.detail:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMRIGHT", 8, 1)
            row.detail:SetPoint("RIGHT", -4, 0)
            row.detail:SetJustifyH("LEFT")
            row:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                if self.set then
                    GameTooltip:SetText(self.set.name)
                    GameTooltip:AddLine("TransmogSet " .. self.set.id, 1, 1, 1)
                    GameTooltip:AddLine(string.format("%d of %d appearances owned", self.set.owned, self.set.total),
                        self.set.complete and .2 or 1, self.set.complete and 1 or .82, .2)
                elseif self.item then
                    GameTooltip:SetHyperlink("item:" .. self.item)
                    GameTooltip:AddLine("ItemModifiedAppearance " .. tostring(self.ima), .5, .8, 1)
                end
                GameTooltip:Show()
            end)
            row:SetScript("OnLeave", function() GameTooltip:Hide() end)
            UI.rows[index] = row
        end

        UI.prev = CreateFrame("Button", nil, body, "UIPanelButtonTemplate")
        UI.prev:SetSize(70, 22)
        UI.prev:SetPoint("BOTTOMLEFT", 12, 10)
        UI.prev:SetText("Previous")
        UI.prev:SetScript("OnClick", function() UI.page = math.max(1, UI.page - 1); Refresh() end)
        UI.next = CreateFrame("Button", nil, body, "UIPanelButtonTemplate")
        UI.next:SetSize(70, 22)
        UI.next:SetPoint("BOTTOMRIGHT", -12, 10)
        UI.next:SetText("Next")
        UI.next:SetScript("OnClick", function() UI.page = UI.page + 1; Refresh() end)
        UI.pageText = body:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        UI.pageText:SetPoint("BOTTOM", 0, 15)

        WardrobeCollectionFrameTab1:SetScript("OnClick", function() ShowMode("items") end)
        WardrobeCollectionFrameTab2:SetScript("OnClick", function() ShowMode("sets") end)
        WardrobeCollectionFrame:HookScript("OnShow", function()
            HideLegacyContent()
            body:Show()
            if wxlwow.request_transmog_collection then wxlwow.request_transmog_collection() end
            Refresh()
        end)
        UI.installed = true
        ShowMode("items")
        return true
    end

    local driver = CreateFrame("Frame")
    driver:SetScript("OnUpdate", function(self, elapsed)
        UI.elapsed = UI.elapsed + elapsed
        if UI.elapsed < .25 then return end
        UI.elapsed = 0
        if not UI.installed then TryInstall() end
        if UI.installed and UI.body:IsShown() then
            local generation = wxlwow.transmog_collection_generation and
                wxlwow.transmog_collection_generation() or 0
            if generation ~= UI.generation then Refresh() end
        end
    end)
end
)lua";

    constexpr const char* kCollectionUiBootstrapV2 = R"lua(
do
    local UI = { installed = false, mode = "items", page = 1, pageSize = 10,
        generation = -1, elapsed = 0, sets = nil, filteredSets = nil,
        selectedSet = nil, searchText = "" }
    local QUESTION = "Interface\\Icons\\INV_Misc_QuestionMark"
    local INVTYPE_TO_SLOT = {
        [1] = 1, [3] = 2, [4] = 5, [5] = 4, [20] = 4,
        [6] = 10, [7] = 11, [8] = 12, [9] = 7, [10] = 9,
        [16] = 3, [19] = 6, [13] = 14, [17] = 14, [21] = 14,
        [14] = 16, [22] = 16, [23] = 16, [15] = 18,
        [25] = 18, [26] = 18, [28] = 18,
    }
    local COLLECTION_SLOTS = { 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 14, 16, 18 }

    local function HideLegacySets()
        if WardrobeCollectionFrameSetsCollectionFrame then
            WardrobeCollectionFrameSetsCollectionFrame:Hide()
        end
        if WardrobeCollectionFrameSetsTransmogFrame then
            WardrobeCollectionFrameSetsTransmogFrame:Hide()
        end
    end

    local function BuildOwnedItems()
        local seen = {}
        for _, slot in ipairs(COLLECTION_SLOTS) do
            _G["TRANSMOG_COLLECTION_" .. slot] = {}
            seen[slot] = {}
        end
        local total = wxlwow.transmog_collection_count and
            wxlwow.transmog_collection_count() or 0
        local added = 0
        for index = 1, total do
            local ima, item, modifier, appearance, display, icon, inventoryType =
                wxlwow.transmog_collection_appearance(index)
            local slot = INVTYPE_TO_SLOT[inventoryType]
            if slot and item and item > 0 and not seen[slot][item] then
                seen[slot][item] = true
                table.insert(_G["TRANSMOG_COLLECTION_" .. slot], item)
                added = added + 1
            end
        end
        for _, slot in ipairs(COLLECTION_SLOTS) do
            table.sort(_G["TRANSMOG_COLLECTION_" .. slot])
        end
        UI.ownedItemCount = added
    end

    local function BuildSetCache()
        UI.sets = {}
        if not wxlwow.transmog_set_count or not wxlwow.transmog_set_info then return end
        local count = wxlwow.transmog_set_count() or 0
        for index = 1, count do
            local id, name, owned, total, complete, quest, parent, group, flags, expansion, order =
                wxlwow.transmog_set_info(index)
            if id then
                table.insert(UI.sets, { index = index, id = id,
                    name = name or ("Set " .. id), owned = owned or 0,
                    total = total or 0, complete = complete, quest = quest or 0,
                    parent = parent or 0, group = group or 0, flags = flags or 0,
                    expansion = expansion or 0, order = order or 0 })
            end
        end
        table.sort(UI.sets, function(a, b)
            if a.complete ~= b.complete then return a.complete end
            local ap, bp = a.owned > 0, b.owned > 0
            if ap ~= bp then return ap end
            if a.owned ~= b.owned then return a.owned > b.owned end
            if a.name ~= b.name then return a.name < b.name end
            return a.id < b.id
        end)
    end

    local function FilterSets()
        UI.filteredSets = {}
        local query = string.lower(UI.searchText or "")
        for _, set in ipairs(UI.sets or {}) do
            if query == "" or string.find(string.lower(set.name), query, 1, true)
                or string.find(tostring(set.id), query, 1, true) then
                table.insert(UI.filteredSets, set)
            end
        end
    end

    local function FindOwnedMemberIcon(set)
        if not wxlwow.transmog_set_member then return QUESTION end
        for member = 1, set.total do
            local ima, owned, item, modifier, appearance, display, icon =
                wxlwow.transmog_set_member(set.index, member)
            if owned and icon then return icon end
        end
        return QUESTION
    end

    local SelectSet

    local function RefreshSetList()
        if not UI.installed or UI.mode ~= "sets" or not UI.body:IsShown() then return end
        for _, row in ipairs(UI.rows) do
            row:Hide()
            row.set = nil
        end

        local loaded, setCount, err = wxlwow.transmog_set_catalog_status()
        if not loaded then
            UI.status:SetText("Retail set catalog unavailable: " .. tostring(err or "unknown error"))
            UI.pageText:SetText("")
            UI.detailName:SetText("Set catalog unavailable")
            UI.detailProgress:SetText("")
            UI.model:Hide()
            return
        end

        FilterSets()
        local total = #(UI.filteredSets or {})
        local pages = math.max(1, math.ceil(total / UI.pageSize))
        if UI.page > pages then UI.page = pages end
        local first = (UI.page - 1) * UI.pageSize + 1
        local selectedVisible = false
        for slot = 1, UI.pageSize do
            local set = UI.filteredSets[first + slot - 1]
            local row = UI.rows[slot]
            if set then
                row.set = set
                row.icon:SetTexture(FindOwnedMemberIcon(set))
                row.name:SetText(set.name)
                row.progressText:SetText(string.format("%d / %d", set.owned, set.total))
                local fraction = set.total > 0 and set.owned / set.total or 0
                row.progress:SetWidth(math.max(1, 218 * fraction))
                if set.complete then
                    row.name:SetTextColor(.2, 1, .2)
                elseif set.owned > 0 then
                    row.name:SetTextColor(1, .82, 0)
                else
                    row.name:SetTextColor(.65, .65, .65)
                end
                local selected = UI.selectedSet and UI.selectedSet.id == set.id
                if selected then selectedVisible = true; row.selected:Show() else row.selected:Hide() end
                row:Show()
            end
        end

        UI.status:SetText(string.format("Sets %d   Collected sets first", total))
        UI.pageText:SetText(string.format("Page %d / %d", UI.page, pages))
        if UI.page > 1 then UI.prev:Enable() else UI.prev:Disable() end
        if UI.page < pages then UI.next:Enable() else UI.next:Disable() end
        if not UI.selectedSet or not selectedVisible then
            SelectSet(UI.filteredSets[first])
        end
        if WardrobeCollectionFrameMixin_UpdateProgressBar then
            local complete = 0
            for _, set in ipairs(UI.sets or {}) do if set.complete then complete = complete + 1 end end
            WardrobeCollectionFrameMixin_UpdateProgressBar(WardrobeCollectionFrame, complete, setCount or total)
        end
    end

    SelectSet = function(set)
        UI.selectedSet = set
        for _, button in ipairs(UI.memberButtons) do
            button:Hide(); button.item = nil; button.ima = nil
        end
        if not set then
            UI.detailName:SetText("No matching sets")
            UI.detailProgress:SetText("")
            UI.model:Hide()
            return
        end

        UI.detailName:SetText(set.name)
        UI.detailProgress:SetText(string.format("TransmogSet %d     %d of %d appearances%s",
            set.id, set.owned, set.total, set.complete and "     Collected" or ""))
        UI.model:Show()
        if UI.model.SetUnit then UI.model:SetUnit("player") end
        if UI.model.Undress then UI.model:Undress()
        elseif UI.model.Dress then UI.model:Dress() end

        for member = 1, set.total do
            local ima, owned, item, modifier, appearance, display, icon, inventoryType =
                wxlwow.transmog_set_member(set.index, member)
            local button = UI.memberButtons[member]
            if button then
                button.ima, button.item, button.owned = ima, item, owned
                button.icon:SetTexture(icon or QUESTION)
                if owned then
                    button.icon:SetDesaturated(false)
                    button.border:SetVertexColor(.15, 1, .15, 1)
                else
                    button.icon:SetDesaturated(true)
                    button.border:SetVertexColor(.45, .45, .45, 1)
                end
                button:Show()
            end
            if owned and item and item > 0 and UI.model.TryOn then
                pcall(UI.model.TryOn, UI.model, item)
            end
        end
        for _, row in ipairs(UI.rows) do
            if row.set and row.set.id == set.id then row.selected:Show() else row.selected:Hide() end
        end
    end

    local function RefreshItems()
        if not WardrobeCollectionFrameItemsCollectionFrame then return end
        WardrobeCollectionFrameItemsCollectionFrame.page = 1
        local selected = TRANSMOG_ITEMS_COLLECTION_SELECT_CHOICE or 1
        if WardrobeItemsModelUpdateVendors then
            pcall(WardrobeItemsModelUpdateVendors, selected)
        end
    end

    local function ShowMode(mode)
        UI.mode = mode
        UI.page = 1
        PanelTemplates_SetTab(WardrobeCollectionFrame, mode == "items" and 1 or 2)
        if mode == "items" then
            UI.body:Hide()
            HideLegacySets()
            WardrobeCollectionFrameItemsCollectionFrame:Show()
            if WardrobeCollectionFrame.SearchBox then
                WardrobeCollectionFrame.SearchBox:ClearAllPoints()
                WardrobeCollectionFrame.SearchBox:SetPoint("TOPRIGHT", -107, -35)
                WardrobeCollectionFrame.SearchBox:SetWidth(115)
            end
            if WardrobeCollectionFrame.FilterButton then WardrobeCollectionFrame.FilterButton:Show() end
            RefreshItems()
        else
            WardrobeCollectionFrameItemsCollectionFrame:Hide()
            HideLegacySets()
            UI.body:Show()
            if WardrobeCollectionFrame.SearchBox then
                WardrobeCollectionFrame.SearchBox:ClearAllPoints()
                WardrobeCollectionFrame.SearchBox:SetPoint("TOPLEFT", 19, -69)
                WardrobeCollectionFrame.SearchBox:SetWidth(225)
                UI.searchText = WardrobeCollectionFrame.SearchBox:GetText() or ""
            end
            if WardrobeCollectionFrame.FilterButton then WardrobeCollectionFrame.FilterButton:Hide() end
            RefreshSetList()
        end
    end

    local function TryInstall()
        if UI.installed then return true end
        if not CollectionsJournalTab5 or not CollectionsJournalTab6 or
            not WardrobeCollectionFrame or not WardrobeCollectionFrameTab1 or
            not WardrobeCollectionFrameTab2 or
            not WardrobeCollectionFrameItemsCollectionFrame then return false end

        CollectionsJournalTab5:SetText(WARDROBE or "Appearances")
        CollectionsJournalTab5:Show()
        CollectionsJournalTab6:ClearAllPoints()
        CollectionsJournalTab6:SetPoint("LEFT", CollectionsJournalTab5, "RIGHT", -16, 0)
        WardrobeCollectionFrameTab1:SetText("Items")
        WardrobeCollectionFrameTab2:SetText("Sets")
        WardrobeCollectionFrameTab2:Show()

        local body = CreateFrame("Frame", "WXLTransmogCollectionBodyV2", WardrobeCollectionFrame)
        body:SetPoint("TOPLEFT", 7, -60)
        body:SetPoint("BOTTOMRIGHT", -6, 6)
        body:SetFrameLevel(WardrobeCollectionFrame:GetFrameLevel() + 20)
        body:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true, tileSize = 16,
            edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
        body:SetBackdropColor(.035, .035, .035, .96)
        UI.body = body

        local divider = body:CreateTexture(nil, "BORDER")
        divider:SetTexture(.35, .28, .15, .9)
        divider:SetWidth(1)
        divider:SetPoint("TOPLEFT", 270, -5)
        divider:SetPoint("BOTTOMLEFT", 270, 5)

        UI.status = body:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
        UI.status:SetPoint("TOPLEFT", 13, -13)
        UI.status:SetWidth(238)
        UI.status:SetJustifyH("LEFT")

        UI.rows = {}
        for index = 1, UI.pageSize do
            local row = CreateFrame("Button", "WXLTransmogSetRowV2" .. index, body)
            row:SetSize(232, 39)
            row:SetPoint("TOPLEFT", 13, -38 - (index - 1) * 41)
            row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
            row.background = row:CreateTexture(nil, "BACKGROUND")
            row.background:SetAllPoints()
            row.background:SetTexture(0, 0, 0, .35)
            row.selected = row:CreateTexture(nil, "ARTWORK")
            row.selected:SetAllPoints()
            row.selected:SetTexture(.35, .22, .03, .55)
            row.selected:Hide()
            row.icon = row:CreateTexture(nil, "ARTWORK")
            row.icon:SetSize(34, 34)
            row.icon:SetPoint("LEFT", 3, 0)
            row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
            row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 7, -3)
            row.name:SetPoint("RIGHT", -5, 0)
            row.name:SetJustifyH("LEFT")
            row.progressText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            row.progressText:SetPoint("BOTTOMLEFT", row.icon, "BOTTOMRIGHT", 7, 3)
            row.progress = row:CreateTexture(nil, "BORDER")
            row.progress:SetHeight(2)
            row.progress:SetPoint("BOTTOMLEFT", 7, 1)
            row.progress:SetTexture(.05, .65, .05, .85)
            row:SetScript("OnClick", function(self) SelectSet(self.set); RefreshSetList() end)
            row:SetScript("OnEnter", function(self)
                if not self.set then return end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetText(self.set.name)
                GameTooltip:AddLine("TransmogSet " .. self.set.id, 1, 1, 1)
                GameTooltip:AddLine(string.format("%d of %d appearances owned",
                    self.set.owned, self.set.total), 1, .82, 0)
                GameTooltip:Show()
            end)
            row:SetScript("OnLeave", function() GameTooltip:Hide() end)
            UI.rows[index] = row
        end

        UI.prev = CreateFrame("Button", nil, body, "UIPanelButtonTemplate")
        UI.prev:SetSize(70, 21)
        UI.prev:SetPoint("BOTTOMLEFT", 13, 10)
        UI.prev:SetText("Previous")
        UI.prev:SetScript("OnClick", function() UI.page = math.max(1, UI.page - 1); RefreshSetList() end)
        UI.next = CreateFrame("Button", nil, body, "UIPanelButtonTemplate")
        UI.next:SetSize(70, 21)
        UI.next:SetPoint("BOTTOMLEFT", 175, 10)
        UI.next:SetText("Next")
        UI.next:SetScript("OnClick", function() UI.page = UI.page + 1; RefreshSetList() end)
        UI.pageText = body:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        UI.pageText:SetPoint("BOTTOM", body, "BOTTOMLEFT", 129, 15)

        UI.detailName = body:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        UI.detailName:SetPoint("TOPLEFT", 288, -18)
        UI.detailName:SetPoint("TOPRIGHT", -15, -18)
        UI.detailName:SetJustifyH("CENTER")
        UI.detailProgress = body:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
        UI.detailProgress:SetPoint("TOP", UI.detailName, "BOTTOM", 0, -5)

        UI.model = CreateFrame("DressUpModel", "WXLTransmogSetPreviewModel", body)
        UI.model:SetPoint("TOPLEFT", 286, -70)
        UI.model:SetPoint("BOTTOMRIGHT", -12, 108)
        UI.model:SetUnit("player")

        UI.memberButtons = {}
        for index = 1, 24 do
            local button = CreateFrame("Button", "WXLTransmogSetMemberV2" .. index, body)
            button:SetSize(34, 34)
            local column = math.mod(index - 1, 10)
            local row = math.floor((index - 1) / 10)
            button:SetPoint("BOTTOMLEFT", 298 + column * 38, 63 - row * 38)
            button.icon = button:CreateTexture(nil, "ARTWORK")
            button.icon:SetAllPoints()
            button.border = button:CreateTexture(nil, "OVERLAY")
            button.border:SetTexture("Interface\\Buttons\\UI-ActionButton-Border")
            button.border:SetBlendMode("ADD")
            button.border:SetSize(54, 54)
            button.border:SetPoint("CENTER")
            button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
            button:SetScript("OnEnter", function(self)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                if self.item and self.item > 0 then GameTooltip:SetHyperlink("item:" .. self.item)
                else GameTooltip:SetText("Uncollected appearance") end
                GameTooltip:AddLine("ItemModifiedAppearance " .. tostring(self.ima or 0), .5, .8, 1)
                GameTooltip:Show()
            end)
            button:SetScript("OnLeave", function() GameTooltip:Hide() end)
            button:Hide()
            UI.memberButtons[index] = button
        end

        local search = WardrobeCollectionFrame.SearchBox
        if search then
            local oldChanged = search:GetScript("OnTextChanged")
            search:SetScript("OnTextChanged", function(self, ...)
                if UI.mode == "sets" then
                    UI.searchText = self:GetText() or ""
                    UI.page = 1
                    RefreshSetList()
                elseif oldChanged then
                    oldChanged(self, ...)
                end
            end)
        end

        WardrobeCollectionFrameTab1:SetScript("OnClick", function() ShowMode("items") end)
        WardrobeCollectionFrameTab2:SetScript("OnClick", function() ShowMode("sets") end)
        WardrobeCollectionFrame:HookScript("OnShow", function()
            if wxlwow.request_transmog_collection then wxlwow.request_transmog_collection() end
            ShowMode(UI.mode)
        end)
        UI.installed = true
        BuildOwnedItems()
        BuildSetCache()
        if wxlwow.request_transmog_collection then wxlwow.request_transmog_collection() end
        ShowMode("items")
        return true
    end

    local driver = CreateFrame("Frame")
    driver:SetScript("OnUpdate", function(self, elapsed)
        UI.elapsed = UI.elapsed + elapsed
        if UI.elapsed < .25 then return end
        UI.elapsed = 0
        if not UI.installed then TryInstall() end
        if UI.installed then
            local generation = wxlwow.transmog_collection_generation and
                wxlwow.transmog_collection_generation() or 0
            if generation ~= UI.generation then
                UI.generation = generation
                BuildOwnedItems()
                BuildSetCache()
                UI.page = 1
                if UI.mode == "items" then RefreshItems() else RefreshSetList() end
            end
        end
    end)
end
)lua";
}

bool wxl_retail_ui::InstallRetailTransmog()
{
    const WXL_NetworkApi* network = Network();
    const WXL_FrameScriptApi* framescript = FrameScript();
    bool ok = true;
    if (!LoadTransmogSetCatalog())
        WLOG_WARN("retail-transmog: set catalog unavailable: %s", g_transmogSetError.c_str());
    ok &= network->RegisterClientOpcode(opcodes::CmsgTransmogCollection,
        "CMSG_WXL_TRANSMOG_COLLECTION") != 0;
    ok &= network->RegisterServerOpcode(opcodes::SmsgTransmogCollection,
        "SMSG_WXL_TRANSMOG_COLLECTION", &OnCollection, nullptr) != 0;
    ok &= network->RegisterClientOpcode(opcodes::CmsgTransmogApply,
        "CMSG_WXL_TRANSMOG_APPLY") != 0;
    ok &= network->RegisterServerOpcode(opcodes::SmsgTransmogResult,
        "SMSG_WXL_TRANSMOG_RESULT", &OnApplyResult, nullptr) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_REQUEST_TRANSMOG_COLLECTION", &LuaRequestCollection) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_COLLECTION_COUNT", &LuaCollectionCount) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_COLLECTION_GENERATION", &LuaCollectionGeneration) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_COLLECTION_APPEARANCE", &LuaCollectionAppearance) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_SET_COUNT", &LuaTransmogSetCount) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_SET_INFO", &LuaTransmogSetInfo) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_SET_MEMBER", &LuaTransmogSetMember) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_SET_CATALOG_STATUS", &LuaTransmogSetCatalogStatus) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_APPLY_TRANSMOG", &LuaApply) != 0;
    ok &= framescript->RegisterFunction("_WXLWOW_TRANSMOG_RESULT", &LuaLastResult) != 0;
    ok &= framescript->RegisterScript("retail-transmog", kBootstrap) != 0;
    // The wardrobe presentation is owned by FrameNew/Colection/RetailCollections.lua.
    // Keep this extension limited to native DB2/opcode-backed Lua services so UI
    // revisions do not require rebuilding or replacing the native bridge.
    return ok;
}
