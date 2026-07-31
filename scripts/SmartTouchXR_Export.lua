-- SmartTouchXR diagnostic bridge for DCS World
-- Repeats diagnostics after cockpit initialization so UDP messages are not lost
-- if the OpenXR layer starts listening after Export.lua is loaded.

local socket = require("socket")

local udp = socket.udp()
udp:settimeout(0)
udp:setpeername("127.0.0.1", 34343)

local function send(message)
    if udp then
        udp:send(message)
    end
end

local frameCount = 0
local lastReportedDevicesSource = nil
local lastReportedDeviceValues = {}

local function loadDevices()
    if type(_G.devices) == "table" then
        return _G.devices, "global"
    end

    if not LockOn_Options or not LockOn_Options.script_path then
        return nil, "no_script_path"
    end

    local ok, result = pcall(
        dofile,
        LockOn_Options.script_path .. "devices.lua"
    )

    if not ok then
        return nil, "devices_lua_error"
    end

    if type(result) == "table" then
        return result, "return_value"
    end

    if type(_G.devices) == "table" then
        return _G.devices, "global_after_dofile"
    end

    return nil, "unavailable"
end

local function reportDevice(name, value)
    local encoded = value ~= nil and tostring(value) or "MISSING"

    if lastReportedDeviceValues[name] == encoded then
        return
    end

    lastReportedDeviceValues[name] = encoded

    if value ~= nil then
        send("SMARTTOUCHXR_DEVICE_AVAILABLE;" .. name .. ";" .. encoded)
    else
        send("SMARTTOUCHXR_DEVICE_MISSING;" .. name)
    end
end

local function reportDiagnostics()
    local devices, source = loadDevices()

    if source ~= lastReportedDevicesSource then
        lastReportedDevicesSource = source
        send("SMARTTOUCHXR_DEVICES_SOURCE;" .. source)
    end

    if devices then
        reportDevice("MFCD_LEFT", devices.MFCD_LEFT)
        reportDevice("MFCD_RIGHT", devices.MFCD_RIGHT)
        reportDevice("UFC", devices.UFC)
        reportDevice("CDU", devices.CDU)
    else
        reportDevice("MFCD_LEFT", nil)
        reportDevice("MFCD_RIGHT", nil)
        reportDevice("UFC", nil)
        reportDevice("CDU", nil)
    end
end

-- This message may be sent before the OpenXR layer begins listening.
send("SMARTTOUCHXR_LUA_LOADED")

local previousAfterNextFrame = LuaExportAfterNextFrame

LuaExportAfterNextFrame = function()
    if previousAfterNextFrame then
        previousAfterNextFrame()
    end

    frameCount = frameCount + 1

    if frameCount == 1 then
        send("SMARTTOUCHXR_FIRST_FRAME")
    end

    -- Repeat a heartbeat and device probe every 300 export frames.
    -- This ensures messages arrive after the OpenXR layer starts listening
    -- and after the A-10C II cockpit devices have initialized.
    if frameCount % 300 == 0 then
        send("SMARTTOUCHXR_HEARTBEAT;" .. tostring(frameCount))
        reportDiagnostics()
    end
end

local previousStop = LuaExportStop

LuaExportStop = function()
    send("SMARTTOUCHXR_STOP")

    if udp then
        udp:close()
        udp = nil
    end

    if previousStop then
        previousStop()
    end
end
