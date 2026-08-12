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

local previous = {
    MASTER_CAUTION = nil,
    LEFT_MFCD_OSB1 = nil,
    RIGHT_MFCD_OSB1 = nil,
}

local mainPanel = nil
local frameCount = 0
local initialized = false
local lastHeartbeatFrame = 0
local cockpitActive = false
local cockpitAircraftName = nil

local function send(message)
    if udp then
        udp:send(message)
    end
end


local function getSelfDataSafe()
    if type(LoGetSelfData) ~= "function" then
        return nil
    end

    local ok, data = pcall(LoGetSelfData)
    if not ok or type(data) ~= "table" then
        return nil
    end

    return data
end

local function resetCockpitArgumentState()
    mainPanel = nil
    initialized = false

    previous.MASTER_CAUTION = nil
    previous.LEFT_MFCD_OSB1 = nil
    previous.RIGHT_MFCD_OSB1 = nil
end

local function updateCockpitState()
    local selfData = getSelfDataSafe()
    local nowActive = selfData ~= nil
    local aircraftName = nil

    if nowActive then
        aircraftName = tostring(selfData.Name or "UNKNOWN")
    end

    if nowActive and not cockpitActive then
        cockpitActive = true
        cockpitAircraftName = aircraftName
        resetCockpitArgumentState()
        send("COCKPIT_ENTERED;" .. cockpitAircraftName)
        return
    end

    if not nowActive and cockpitActive then
        send("COCKPIT_LEFT")
        cockpitActive = false
        cockpitAircraftName = nil
        resetCockpitArgumentState()
        return
    end

    -- Direct slot/module change without an observable nil state.
    if nowActive and cockpitActive and aircraftName ~= cockpitAircraftName then
        send("COCKPIT_LEFT")
        cockpitAircraftName = aircraftName
        resetCockpitArgumentState()
        send("COCKPIT_ENTERED;" .. cockpitAircraftName)
    end
end

local CONFIG = {
    MASTER_CAUTION = 403,
    LEFT_MFCD_OSB1 = 300,
    RIGHT_MFCD_OSB1 = 326,
}

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

    updateCockpitState()

    if not cockpitActive then
        if frameCount - lastHeartbeatFrame >= 600 then
            lastHeartbeatFrame = frameCount
            send("SMARTTOUCHXR_SPECTATOR_HEARTBEAT")
        end
        return
    end

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


local previousHumanStart = LuaExportOnHumanStart

LuaExportOnHumanStart = function()
    -- DCS calls this when the player takes control of a human aircraft.
    updateCockpitState()

    if previousHumanStart then
        previousHumanStart()
    end
end

local previousStop = LuaExportStop

LuaExportStop = function()
    -- DCS may leave LoGetSelfData() valid right up until the mission/export
    -- context is torn down. Explicitly tell SmartTouchXR that cockpit mode
    -- is over so cockpit-only HUD/markers do not survive into the main menu.
    if cockpitActive then
        send("COCKPIT_LEFT")
        cockpitActive = false
        cockpitAircraftName = nil
        resetCockpitArgumentState()
    end

    send("SMARTTOUCHXR_CALIBRATION_SCRIPT_STOP")

    if udp then
        udp:close()
        udp = nil
    end

    if previousStop then
        previousStop()
    end
end
