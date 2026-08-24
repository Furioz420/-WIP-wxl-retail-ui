// Reusable retail FrameScript compatibility shims for the 3.3.5a client.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "ExtensionApi.hpp"

namespace
{
    // Every definition is conditional so Patch-W or another extension can supply
    // a more complete implementation without being overwritten here.
    constexpr char kBootstrap[] = R"lua(
do
    wxlwow = wxlwow or {}
    wxlwow.retail_ui = wxlwow.retail_ui or {}
    local Compat = wxlwow.retail_ui
    Compat.apiVersion = 2

    C_UI = C_UI or {}
    if type(C_UI.GetUIParent) ~= "function" then
        function C_UI.GetUIParent() return UIParent end
    end
    if type(C_UI.GetWorldFrame) ~= "function" then
        function C_UI.GetWorldFrame() return WorldFrame end
    end

    InputUtil = InputUtil or {}
    if type(InputUtil.GetCursorPosition) ~= "function" then
        function InputUtil.GetCursorPosition(relativeTo)
            local x, y = GetCursorPosition()
            local frame = relativeTo or UIParent
            local scale = frame and frame.GetEffectiveScale and frame:GetEffectiveScale() or 1
            if not scale or scale == 0 then scale = 1 end
            return x / scale, y / scale
        end
    end

    if type(securecallfunction) ~= "function" then
        function securecallfunction(func, ...)
            if type(func) ~= "function" then return nil end
            return func(...)
        end
    end

    Settings = Settings or {}
    local settingValues = Compat.settingValues or {}
    Compat.settingValues = settingValues
    if type(Settings.GetValue) ~= "function" then
        function Settings.GetValue(key) return settingValues[key] end
    end
    if type(Settings.SetValue) ~= "function" then
        function Settings.SetValue(key, value)
            settingValues[key] = value
            return value
        end
    end

    C_Timer = C_Timer or {}
    if type(C_Timer.NewTimer) ~= "function" then
        function C_Timer.NewTimer(duration, callback)
            local timer = CreateFrame("Frame")
            timer.remaining = math.max(tonumber(duration) or 0, 0)
            timer.cancelled = false
            function timer:Cancel()
                self.cancelled = true
                self:SetScript("OnUpdate", nil)
                self:Hide()
            end
            function timer:IsCancelled() return self.cancelled end
            timer:SetScript("OnUpdate", function(self, elapsed)
                self.remaining = self.remaining - elapsed
                if self.remaining <= 0 then
                    self:SetScript("OnUpdate", nil)
                    self:Hide()
                    if not self.cancelled and type(callback) == "function" then callback() end
                end
            end)
            return timer
        end
    end
    if type(C_Timer.After) ~= "function" then
        function C_Timer.After(duration, callback)
            C_Timer.NewTimer(duration, callback)
        end
    end

    if type(CreateFramePool) ~= "function" then
        function CreateFramePool(frameType, parent, template, resetterFunc)
            local pool = { active = {}, inactive = {} }
            function pool:Acquire()
                local frame = table.remove(self.inactive)
                if not frame then frame = CreateFrame(frameType or "Frame", nil, parent, template) end
                self.active[frame] = true
                return frame, true
            end
            function pool:Release(frame)
                if not frame or not self.active[frame] then return end
                self.active[frame] = nil
                if type(resetterFunc) == "function" then resetterFunc(self, frame) end
                frame:Hide()
                table.insert(self.inactive, frame)
            end
            function pool:ReleaseAll()
                local release = {}
                for frame in pairs(self.active) do table.insert(release, frame) end
                for _, frame in ipairs(release) do self:Release(frame) end
            end
            function pool:EnumerateActive() return pairs(self.active) end
            return pool
        end
    end

    -- A translated retail AnimationGroup can consist of several native 3.3.5
    -- groups, one per child region.  This proxy preserves the object surface
    -- used by Blizzard Lua while the transpiler keeps the animations native.
    if type(Compat.CreateAnimationProxy) ~= "function" then
        function Compat.CreateAnimationProxy(owner, components, options)
            components = components or {}
            options = options or {}
            local proxy = {
                owner = owner,
                components = components,
                options = options,
                elapsed = 0,
                playing = false,
                paused = false,
                baseDimensions = {},
            }
            local driver = CreateFrame("Frame")
            driver:Hide()
            proxy.driver = driver

            local function applyStates(states)
                if type(states) ~= "table" then return end
                for _, state in ipairs(states) do
                    local target = state and state.target
                    if target and type(target.SetAlpha) == "function" and state.alpha ~= nil then
                        target:SetAlpha(state.alpha)
                    end
                end
            end

            local function applyScaleStates(states)
                if type(states) ~= "table" then return end
                for _, state in ipairs(states) do
                    local target = state and state.target
                    if target and type(target.GetWidth) == "function" and type(target.SetWidth) == "function" then
                        local dimensions = proxy.baseDimensions[target]
                        if not dimensions then
                            dimensions = { width = target:GetWidth(), height = target:GetHeight() }
                            proxy.baseDimensions[target] = dimensions
                        end
                        target:SetWidth(dimensions.width * (tonumber(state.scaleX) or 1))
                        target:SetHeight(dimensions.height * (tonumber(state.scaleY) or 1))
                    end
                end
            end

            driver:SetScript("OnUpdate", function(_, elapsed)
                if not proxy.playing or proxy.paused then return end
                proxy.elapsed = proxy.elapsed + elapsed
                local duration = tonumber(options.duration) or 0
                if not options.looping and proxy.elapsed >= duration then
                    proxy.playing = false
                    driver:Hide()
                    if options.setToFinalAlpha then applyStates(options.finalAlpha) end
                    applyScaleStates(options.finalScale)
                    if type(options.onFinished) == "function" then
                        pcall(options.onFinished, proxy)
                    end
                end
            end)

            function proxy:Play()
                self.elapsed = 0
                self.playing = true
                self.paused = false
                applyStates(options.initialAlpha)
                applyScaleStates(options.initialScale)
                if type(options.onPlay) == "function" then
                    pcall(options.onPlay, self)
                end
                for _, component in ipairs(components) do
                    if component and type(component.Play) == "function" then component:Play() end
                end
                if not options.looping then driver:Show() end
            end

            function proxy:Restart()
                self:Stop()
                self:Play()
            end

            function proxy:Stop()
                for _, component in ipairs(components) do
                    if component and type(component.Stop) == "function" then component:Stop() end
                end
                self.playing = false
                self.paused = false
                self.elapsed = 0
                driver:Hide()
            end

            function proxy:Pause()
                for _, component in ipairs(components) do
                    if component and type(component.Pause) == "function" then component:Pause() end
                end
                self.paused = true
            end

            function proxy:IsPlaying()
                if self.playing and not self.paused then return true end
                for _, component in ipairs(components) do
                    if component and type(component.IsPlaying) == "function" and component:IsPlaying() then
                        return true
                    end
                end
                return false
            end

            function proxy:IsPaused() return self.paused end
            function proxy:GetParent() return self.owner end
            function proxy:GetDuration() return tonumber(options.duration) or 0 end
            function proxy:GetProgress()
                local duration = tonumber(options.duration) or 0
                if duration <= 0 then return 0 end
                return math.min(self.elapsed / duration, 1)
            end
            return proxy
        end
    end
    if type(WXL_CreateRetailAnimationProxy) ~= "function" then
        function WXL_CreateRetailAnimationProxy(owner, components, options)
            return Compat.CreateAnimationProxy(owner, components, options)
        end
    end

    -- Retail XML permits a texture to remember a scale that is applied after a
    -- later SetAtlas(..., true).  The legacy XML schema cannot express that
    -- attribute, so translated XML stores _wxlRetailAtlasScale on the region.
    local function installAtlasScaleAdapter()
        if Compat.atlasScaleAdapterInstalled then return true end
        if not UIParent or type(UIParent.CreateTexture) ~= "function" then return false end
        local probe = UIParent:CreateTexture(nil, "BACKGROUND")
        local meta = probe and getmetatable(probe)
        local methods = meta and meta.__index
        if type(methods) ~= "table" or type(methods.SetAtlas) ~= "function" then return false end
        if methods._wxlRetailAtlasScaleWrapped then
            Compat.atlasScaleAdapterInstalled = true
            return true
        end
        local originalSetAtlas = methods.SetAtlas
        methods.SetAtlas = function(self, atlasName, useAtlasSize, ...)
            if useAtlasSize == nil and self._wxlRetailUseAtlasSize ~= nil then
                useAtlasSize = self._wxlRetailUseAtlasSize == true
            end
            -- Retail resolves dynamically selected atlas names as part of
            -- SetAtlas.  WotLK's SharedExtendedMethods asserts directly
            -- against S_ATLAS_STORAGE, while our full catalog is lazy.
            if type(atlasName) == "string" and type(WXL_LoadAtlas) == "function"
                    and (type(S_ATLAS_STORAGE) ~= "table" or S_ATLAS_STORAGE[atlasName] == nil) then
                WXL_LoadAtlas(atlasName)
            end
            local result = originalSetAtlas(self, atlasName, useAtlasSize, ...)
            local scale = tonumber(self._wxlRetailAtlasScale)
            if scale and scale > 0 and useAtlasSize then
                self:SetWidth(self:GetWidth() * scale)
                self:SetHeight(self:GetHeight() * scale)
            end
            return result
        end
        methods._wxlRetailAtlasScaleWrapped = true
        Compat.atlasScaleAdapterInstalled = true
        return true
    end
    Compat.InstallAtlasScaleAdapter = installAtlasScaleAdapter
    if not installAtlasScaleAdapter() then
        local installer = CreateFrame("Frame")
        installer:RegisterEvent("VARIABLES_LOADED")
        installer:RegisterEvent("PLAYER_LOGIN")
        installer:SetScript("OnEvent", function(self)
            if installAtlasScaleAdapter() then
                self:UnregisterAllEvents()
                self:Hide()
            end
        end)
    end

    CVarCallbackRegistry = CVarCallbackRegistry or {}
    if type(CVarCallbackRegistry.GetCVarValueBool) ~= "function" then
        function CVarCallbackRegistry:GetCVarValueBool(name)
            local value = GetCVar and GetCVar(name)
            return value == "1" or value == 1 or value == true
        end
    end

    SlashCmdList = SlashCmdList or {}
    -- Keep the native compatibility diagnostic separate from the addon-level
    -- WXL_RetailUI module platform.  Registering the same slash key during
    -- late FrameScript bootstrap used to replace the platform command.
    SLASH_WXLRETAILCOMPAT1 = "/wxlretailcompat"
    SlashCmdList.WXLRETAILCOMPAT = function()
        print("|cff66ccffWXL Retail UI|r: compatibility API v" .. Compat.apiVersion .. " active.")
        print("C_UI=" .. type(C_UI.GetUIParent) ..
              " InputUtil=" .. type(InputUtil.GetCursorPosition) ..
              " Settings=" .. type(Settings.GetValue) ..
              " Timer=" .. type(C_Timer.NewTimer) ..
              " FramePool=" .. type(CreateFramePool) ..
              " AnimProxy=" .. type(WXL_CreateRetailAnimationProxy) ..
              " AtlasScale=" .. tostring(Compat.atlasScaleAdapterInstalled == true))
    end
end
)lua";
}

namespace wxl_retail_ui
{
    bool InstallRetailUiCompatibility()
    {
        const WXL_FrameScriptApi* api = FrameScript();
        if (!api) return false;
        const bool ok = api->RegisterScript("retail-ui-compat", kBootstrap) != 0;
        if (ok) WLOG_INFO("shared retail FrameScript compatibility API registered");
        return ok;
    }
}
