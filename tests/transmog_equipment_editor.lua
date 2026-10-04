
requests, resets, ready, revision, enabled = 0, 0, false, 0, true
function _WXL_TRANSMOG_EQUIPMENT_REQUEST() requests=requests+1; ready=false; return true end
function _WXL_TRANSMOG_EQUIPMENT_RESET() resets=resets+1; ready=false end
function _WXL_TRANSMOG_EQUIPMENT_STATUS() return ready, revision, enabled end
function _WXL_TRANSMOG_EQUIPMENT_SLOT(slot)
    if slot==1 then return 100,82423,1,82423,108655 end
    if slot==2 then return 101,123,2,41833,108655 end
    return 0,0,0,0,0
end
scripts={}
function CreateFrame() return {RegisterEvent=function() end,SetScript=function(_, name, fn) scripts[name]=fn end} end

local path=os.getenv('WXL_TRANSMOG_EDITOR_SOURCE') or '../src/RetailTransmogEquipmentLua.inc'
local f=assert(io.open(path,'r')); local text=f:read('*a'); f:close()
local body=assert(text:match('^R"lua%((.*)%)lua"%s*$'))
assert((loadstring or load)(body))()

local e=WXLTransmogEditor
assert(e.Open() and requests==1)
assert(e.GetSlot(1)==nil and not e.SetPending(1,2,41833))
ready=true; revision=1
assert(e.GetSlot(1).sourceKind==1)
assert(e.SetPending(1,2,41833))
assert(e.GetSlot(1).sourceID==82423 and e.GetPending(1).sourceID==41833)
local copy=e.GetSlot(1); copy.sourceID=999; assert(e.GetSlot(1).sourceID==82423)
assert(not e.SetPending(3,1,100)) -- empty slot
assert(not e.SetPending(1,2,0) and not e.SetPending(1,0,123))
assert(not e.SetPending(1,4,1) and not e.SetPending(1,2,1.5))
assert(e.SetPending(1,1,82423) and e.GetPending(1)==nil)
assert(e.SetPending(1,3,0)); e.ClearPending(); assert(e.GetPending(1)==nil)
assert(e.SetPending(1,2,41833))
scripts.OnEvent(nil,'PLAYER_EQUIPMENT_CHANGED')
assert(e.GetSlot(1)==nil and e.GetPending(1)==nil)
scripts.OnUpdate(); assert(requests==2)
ready=true; revision=2; assert(e.GetSlot(1) and not e.GetPending(1))
e.SetPending(1,2,41833); scripts.OnEvent(nil,'PLAYER_LEAVING_WORLD')
assert(e.GetSlot(1)==nil and e.GetPending(1)==nil)
ready=true; revision=3; assert(e.GetSlot(1)==nil) -- closed session cannot revive
e.Open(); ready=true; revision=4; enabled=false; assert(e.GetSlot(1)==nil)
enabled=true; revision=5
local original=_WXL_TRANSMOG_EQUIPMENT_SLOT
local changed=false
function _WXL_TRANSMOG_EQUIPMENT_SLOT(slot)
    if slot==10 and not changed then revision=6; changed=true end
    return original(slot)
end
assert(e.GetSlot(1)==nil) -- do not publish rows read across two revisions
assert(e.GetSlot(1).sourceID==82423)

print('editor state tests passed')
