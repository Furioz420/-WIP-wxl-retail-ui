R"lua(
-- Private editor state. Retail C_* adapters must be added with their real contracts.
-- All pending edits are local. No acquisition or apply operation is sent here.
WXLTransmogEditor = WXLTransmogEditor or {}
local editor = WXLTransmogEditor
local slots, pending = {}, {}
local active, generation, dirty = false, nil, false

function editor.Close()
    active, dirty, generation = false, false, nil
    slots, pending = {}, {}
    _WXL_TRANSMOG_EQUIPMENT_RESET()
end
function editor.Open()
    editor.Close()
    active = true
    return _WXL_TRANSMOG_EQUIPMENT_REQUEST()
end
function editor.Refresh()
    -- Equipment changes invalidate target identities and pending previews immediately.
    slots, pending = {}, {}
    if active then return _WXL_TRANSMOG_EQUIPMENT_REQUEST() end
end
function editor.Poll()
    if not active then return false end
    local ready, revision, enabled = _WXL_TRANSMOG_EQUIPMENT_STATUS()
    if not ready then return false end
    if generation ~= revision then
        local fresh = {}
        for slot = 1, 19 do
            local guid, item, kind, source, display = _WXL_TRANSMOG_EQUIPMENT_SLOT(slot)
            if guid == nil then return false end
            fresh[slot] = {itemGUID=guid, itemID=item, sourceKind=kind, sourceID=source, displayID=display}
        end
        local stillReady, stillRevision = _WXL_TRANSMOG_EQUIPMENT_STATUS()
        if not stillReady or stillRevision ~= revision then return false end
        slots, pending, generation = fresh, {}, revision
    end
    return enabled
end
function editor.GetSlot(slot)
    if not editor.Poll() then return nil end
    local row = slots[slot]
    if not row then return nil end
    -- Callers cannot mutate the authoritative snapshot.
    return {itemGUID=row.itemGUID, itemID=row.itemID, sourceKind=row.sourceKind,
        sourceID=row.sourceID, displayID=row.displayID}
end
function editor.SetPending(slot, kind, id)
    local row = editor.GetSlot(slot)
    if not row or row.itemGUID == 0 or type(id) ~= 'number' or id ~= math.floor(id) or id < 0 or id > 4294967295 then return false end
    if not ((kind == 0 or kind == 3) and id == 0 or (kind == 1 or kind == 2) and id > 0) then return false end
    if row.sourceKind == kind and row.sourceID == id then pending[slot] = nil
    else pending[slot] = {itemGUID=row.itemGUID, sourceKind=kind, sourceID=id} end
    return true
end
function editor.GetPending(slot)
    if not editor.Poll() then return nil end
    local row = pending[slot]
    if row then return {itemGUID=row.itemGUID, sourceKind=row.sourceKind, sourceID=row.sourceID} end
end
function editor.ClearPending() pending = {} end

local frame = CreateFrame('Frame')
frame:RegisterEvent('PLAYER_ENTERING_WORLD')
frame:RegisterEvent('PLAYER_LEAVING_WORLD')
frame:RegisterEvent('PLAYER_EQUIPMENT_CHANGED')
frame:SetScript('OnEvent', function(_, event)
    if event == 'PLAYER_EQUIPMENT_CHANGED' then
        slots, pending = {}, {}
        _WXL_TRANSMOG_EQUIPMENT_RESET()
        dirty = active
    else editor.Close() end
end)
frame:SetScript('OnUpdate', function()
    if dirty then dirty = false; editor.Refresh() end
end)

)lua"
