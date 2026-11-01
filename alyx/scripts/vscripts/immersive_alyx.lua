-- Immersive Alyx: writes what the fan and the heat lamp need into the console, ImmersiveAlyx.exe reads it from
-- console.log (launch option -condebug) and switches the plugs. Loaded by skill_manifest.cfg:
--   script_reload_code immersive_alyx.lua
-- Lines start with [ImmersiveAlyx], everything else in the console is ignored by the program.

local TAG = "[ImmersiveAlyx]"
local THINK_NAME = "ImmersiveAlyxThink"
local THINK_INTERVAL = 0.1     -- seconds between two checks
local STATE_INTERVAL = 0.25    -- seconds between two state lines (also the heartbeat of the program)
local FIRE_RADIUS = 150        -- units (inches), about 4 m
local SKY_TRACE_LENGTH = 2000  -- nothing above the head within this distance counts as outside
local BLAST_RADIUS = 600       -- explosions within this distance give a gust
local MAX_SPEED = 3000         -- faster position changes are teleports

-- entities that burn: map fires, flames on burning things
local FIRE_CLASSES = { "env_fire", "entityflame", "env_firesource" }
local BLAST_CLASSES = { "env_explosion" }

local lastPosition = nil
local lastTime = 0
local lastStateTime = -1
local smoothedSpeed = 0
local knownBlasts = {}

local function Send(text)
	print(TAG .. " " .. text)
end

local function CountWithin(classes, origin, radius)
	local count = 0
	for _, class in ipairs(classes) do
		local ok, found = pcall(Entities.FindAllByClassnameWithin, Entities, class, origin, radius)
		if ok and found then
			count = count + #found
		end
	end
	return count
end

local function IsOutside(player)
	local head = player:EyePosition()
	local trace = { startpos = head, endpos = head + Vector(0, 0, SKY_TRACE_LENGTH), ignore = player }
	local ok = pcall(TraceLine, trace)
	return ok and not trace.hit
end

-- a new explosion near the player: env_explosion entities only live for a moment, each one is reported once
local function CheckBlasts(origin)
	local ok, found = pcall(Entities.FindAllByClassnameWithin, Entities, BLAST_CLASSES[1], origin, BLAST_RADIUS)
	if not ok or not found then
		return
	end
	local seen = {}
	for _, blast in ipairs(found) do
		local index = blast:entindex()
		seen[index] = true
		if not knownBlasts[index] then
			Send(string.format("blast dist=%d", math.floor((blast:GetAbsOrigin() - origin):Length())))
		end
	end
	knownBlasts = seen
end

local function Think()
	local player = Entities:GetLocalPlayer()
	if not player then
		return THINK_INTERVAL
	end

	local now = Time()
	local position = player:GetAbsOrigin()
	if lastPosition and now > lastTime then
		local speed = (position - lastPosition):Length() / (now - lastTime)
		if speed < MAX_SPEED then
			smoothedSpeed = smoothedSpeed + (speed - smoothedSpeed) * 0.5
		end
	end
	lastPosition = position
	lastTime = now

	CheckBlasts(position)

	if now - lastStateTime >= STATE_INTERVAL then
		lastStateTime = now
		local fire = CountWithin(FIRE_CLASSES, position, FIRE_RADIUS) > 0
		Send(string.format("state speed=%d outside=%d fire=%d health=%d",
			math.floor(smoothedSpeed), IsOutside(player) and 1 or 0, fire and 1 or 0, player:GetHealth()))
	end
	return THINK_INTERVAL
end

local function OnPlayerHurt(event)
	-- the keys of this event are logged once, they show what can be used for fire damage later
	local keys = {}
	for key, value in pairs(event) do
		table.insert(keys, tostring(key) .. "=" .. tostring(value))
	end
	Send("hurt " .. table.concat(keys, " "))
end

local function Start()
	local player = Entities:GetLocalPlayer()
	if not player then
		return false
	end
	player:SetContextThink(THINK_NAME, Think, 0)
	Send("started")
	return true
end

-- the script is loaded again on every map load, the old listeners are replaced
if ImmersiveAlyxListeners then
	for _, id in ipairs(ImmersiveAlyxListeners) do
		StopListeningToGameEvent(id)
	end
end
ImmersiveAlyxListeners = {
	ListenToGameEvent("player_hurt", OnPlayerHurt, nil),
	ListenToGameEvent("player_activate", function() Start() end, nil),
	ListenToGameEvent("player_spawn", function() Start() end, nil),
}

if not Start() then
	Send("waiting for the player")
end
