-- SmartTouchXR three-point cockpit calibration bridge
-- A-10C II verified cockpit arguments:
--   MASTER_CAUTION  = 403
--   LEFT_MFCD_OSB1  = 300
--   RIGHT_MFCD_OSB1 = 326
--
-- Loaded by:
-- C:\Users\<user>\Saved Games\DCS.openbeta\Scripts\Export.lua
--
-- The script watches only these three main-panel arguments and sends one UDP
-- calibration event on each rising edge.

local socket = require("socket")

local udp = socket.udp()
udp:settimeout(0)
udp:setpeername("127.0.0.1", 34343)

local function send(message)
    if udp then
        udp:send(message)
    end
end

local CONFIG = {
    MASTER_CAUTION = 403,
    LEFT_MFCD_OSB1 = 300,
    RIGHT_MFCD_OSB1 = 326,
}

local previous = {
    MASTER_CAUTION = nil,
    LEFT_MFCD_OSB1 = nil,
    RIGHT_MFCD_OSB1 = nil,
}

local mainPanel = nil
local frameCount = 0
local initialized = false
local lastHeartbeatFrame = 0

local function getMainPanel()
    if mainPanel then
        return mainPanel
    end

    local ok, device = pcall(GetDevice, 0)
    if ok and device then
        mainPanel = device
    end

    return mainPanel
end

local function readArgument(argument)
    local device = getMainPanel()
    if not device then
        return nil
    end

    local ok, value = pcall(
        device.get_argument_value,
        device,
        argument
    )

    if not ok or type(value) ~= "number" then
        return nil
    end

    return value
end

local function initializeState()
    local allAvailable = true

    for name, argument in pairs(CONFIG) do
        local value = readArgument(argument)

        if value == nil then
            allAvailable = false
        else
            previous[name] = value
        end
    end

    if allAvailable then
        initialized = true
        send("SMARTTOUCHXR_CALIBRATION_READY")
    end
end

local function checkRisingEdge(name, argument)
    local value = readArgument(argument)
    if value == nil then
        return
    end

    local oldValue = previous[name]

    if oldValue ~= nil and value > 0.5 and oldValue <= 0.5 then
        send("CALIBRATE;" .. name)
    end

    previous[name] = value
end

send("SMARTTOUCHXR_CALIBRATION_SCRIPT_LOADED")

local previousAfterNextFrame = LuaExportAfterNextFrame

LuaExportAfterNextFrame = function()
    if previousAfterNextFrame then
        previousAfterNextFrame()
    end

    frameCount = frameCount + 1

    if not initialized then
        initializeState()
        return
    end

    checkRisingEdge("MASTER_CAUTION", CONFIG.MASTER_CAUTION)
    checkRisingEdge("LEFT_MFCD_OSB1", CONFIG.LEFT_MFCD_OSB1)
    checkRisingEdge("RIGHT_MFCD_OSB1", CONFIG.RIGHT_MFCD_OSB1)

    if frameCount - lastHeartbeatFrame >= 600 then
        lastHeartbeatFrame = frameCount
        send("SMARTTOUCHXR_CALIBRATION_HEARTBEAT")
    end
end

local previousStop = LuaExportStop

LuaExportStop = function()
    send("SMARTTOUCHXR_CALIBRATION_SCRIPT_STOP")

    if udp then
        udp:close()
        udp = nil
    end

    if previousStop then
        previousStop()
    end
end
