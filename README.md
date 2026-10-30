# Immersive Winds (Skyrim SE / AE / VR)

An SKSE plugin that drives a real fan through a smart plug: the stronger the wind in the game, the more the fan blows. The level depends on interior or exterior, the location type (house, dungeon, cave, ice cave), the weather (including storms), the time of day, the altitude and the *Immersive Winds Call* shout.

Mod page: https://www.nexusmods.com/skyrimspecialedition/mods/18346

## Requirements

- Skyrim SE, AE or VR with SKSE (SKSE64 or SKSEVR)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) (SE/AE) or [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101) (VR)
- A fan on a smart plug, either
  - a **TP-Link Kasa** plug with the local protocol on port 9999 (e.g. HS100/HS110; some newer firmware no longer offers it), or
  - any plug that can be switched with an **http request** (Shelly, Tasmota, Home Assistant, ...)

## Installation

1. Copy `ImmersiveWindsPluginScript.dll` to `Data/SKSE/Plugins`.
2. Optional: copy `ImmersiveWindsVR.esp` to `Data` and activate it in the load order. It adds the *Immersive Winds Call* shout, the fan control works without it.
3. TP-Link: name the plug **`ImmersiveWindsPlug`** in the Kasa app (or set another name in `sPlugName`). The plugin finds it in the local network and remembers its ip.
   http: see [Http switch](#http-switch).
4. Start the game with `skse64_loader.exe` (VR: `sksevr_loader.exe`).

On the first start the plugin writes all settings with their defaults to
`Documents/My Games/<Skyrim Special Edition|Skyrim VR>/SKSE/immersiveWinds.ini`.
The log is `ImmersiveWindsPluginScript.log` in the same folder (details with `bLoggingEnabled=1`).

## When the fan stops

Whatever the level, the fan is off

- while the game window is not in the foreground (not in VR, the game keeps running there without focus),
- when the game stands still for more than 10 s (frozen, long loading screen),
- after the game exits.

The level is restored as soon as the game continues. Menus only stop the fan with `bNoWindInMenu=1`.

## Settings (`immersiveWinds.ini`)

Missing keys are added with their default value, existing values are never overwritten.

### `[GeneralModConfig]`

| Key | Default | |
|---|---|---|
| `sSwitchType` | `tplink` | `tplink` or `http` |
| `sPlugName` | `ImmersiveWindsPlug` | TP-Link: name of the plug in the Kasa app |
| `bUseUdp` | `1` | TP-Link: send the commands with udp (`1`) or tcp (`0`) |
| `iWindMode` | `0` | `0`: levels 0-6 (below). `1`: simulated wind, see [Simulated wind](#simulated-wind-iwindmode1) |
| `bIntermittentModeEnabled` | `1` | `iWindMode=0` only. `1`: levels 0-6 with on/off patterns. `0`: simple mode, fan on outside, off inside |
| `bNoWindInMenu` | `0` | `1`: fan off while a menu pauses the game |
| `bLoggingEnabled` | `0` | detailed log |

### `[Internal]`

| Key | |
|---|---|
| `sTpLinkPlugIp` | ip of the TP-Link plug, found and written by the plugin. Clear it to search again. |
| `sModVersion` | written by the plugin |

### `[HttpSwitch]`

Used with `sSwitchType=http`, see [Http switch](#http-switch).

| Key | Default | |
|---|---|---|
| `sHttpMethod` | `GET` | `GET`, `POST`, ... |
| `sOnUrl` | | url that switches the plug on |
| `sOffUrl` | | url that switches the plug off |

### `[IntermittendModeLevelConfig]`

The level is the base value of the place plus the modifiers, at most 6. The shout raises it to at least 5 (first word) or 6 (both words).

| Key | Default | |
|---|---|---|
| `iInsideHouseBase` | `0` | houses, inns, shops, castles, ... |
| `iInsideDungeonBase` | `1` | other interiors |
| `iInsideCaveBase` | `1` | caves |
| `iInsideIceCaveBase` | `2` | ice caves |
| `iOutsideBase` | `3` | exterior |
| `iOutsideTimeDayMod` / `iOutsideTimeNightMod` | `0` / `1` | day is 7:00 to 19:00 |
| `iOutsideWeatherPleasantMod` | `0` | |
| `iOutsideWeatherCloudyMod` | `1` | |
| `iOutsideWeatherRainMod` | `1` | |
| `iOutsideWeatherSnowMod` | `2` | |
| `iOutsideWeatherStormMod` | `3` | weathers in `sStormWeathers` |
| `iOutsideAltitudeHighMod` / `iOutsideAltitudeVeryHighMod` | `1` / `2` | above `iHighAltitudeBegin` / `iVeryHighAltitudeBegin` |
| `iHighAltitudeBegin` / `iVeryHighAltitudeBegin` | `4000` / `18000` | height in game units |
| `sStormWeathers` | vanilla storms, True Storms, Vivid Weathers, Mythical Ages | see below |

`sStormWeathers` is a comma separated list of weathers that count as storm:

- `Plugin.esp:FormID` (hex, without load order index): that form of that plugin, e.g. `Skyrim.esm:0C8220`. Works with light plugins.
- `FormID` without plugin: that id in any plugin (how older versions worked). Can also match an unrelated weather of another mod; prefer the plugin form.

### `[IntermittendModeStrengthConfig]`

On/off pattern of the levels 1-6 in seconds: `fIntermittentL<n>On`, `fIntermittentL<n>Off`, plus a random time of up to `fIntermittentL<n>RandomAddOn` / `RandomAddOff`. `On` or `Off` of `0` keeps the plug always off / on in that level, other values are raised to at least 1 second. Level 0 is always off.

## Simulated wind (`iWindMode=1`)

Instead of fixed on/off patterns the plugin simulates gusty wind and switches the plug so the fan follows it:

- **Base wind**: outside the wind speed of the current weather in the game (the value the trees and grass move with, 0-1) times `fGameWindScale`, plus `fOutsideMinWind` and the altitude modifiers of `[IntermittendModeLevelConfig]` (a level step is 1/6). Inside the base level of the place / 6 (house 0, dungeon and cave 1/6, ice cave 2/6). The shout raises it to 0.85 (first word) or full wind (both words).
- **Turbulence**: random variation around the base wind (a mean reverting random process, not Perlin noise).
- **Gusts and lulls**: at random times, each rises and falls softly within a few seconds.
- **Motion wind**: running, riding and falling add wind, from the speed of the player.
- **Fan**: the plugin estimates the fan speed from its spin up and spin down times. It switches on when the fan is slower than the wind and off when it is faster, so a weak wind becomes short pulses and a strong wind long on times.

The gusts are simulated by the plugin, the game has no gust values (tree movement is computed inside the renderer); the strength follows the game. Every switch is a relay click: like the levels, a weak wind switches about every few seconds. Raise `fMinOnSeconds` / `fMinOffSeconds` for fewer switches.

### `[SimulatedWind]`

| Key | Default | |
|---|---|---|
| `fGameWindScale` | `1.0` | factor for the wind of the game |
| `fOutsideMinWind` | `0.1` | wind outside even with calm weather |
| `fTurbulence` | `0.25` | strength of the variation, share of the base wind |
| `fTurbulenceSeconds` | `4.0` | how long a variation lasts on average |
| `fGustsPerMinute` | `3.0` | average number of gusts and lulls |
| `fGustMinSeconds` / `fGustMaxSeconds` | `3.0` / `8.0` | length of a gust |
| `fGustStrength` | `0.6` | strongest gust, share of the base wind |
| `fLullShare` | `0.25` | share of lulls (wind drops) among the gusts |
| `fMotionStartSpeed` | `150` | speed (game units per second) from which movement adds wind; walking is below, running about 300 |
| `fMotionFullSpeed` | `1000` | speed with the full motion wind (galloping horse) |
| `fMotionWindMax` | `1.0` | motion wind at `fMotionFullSpeed`, `0` turns it off |
| `fFanSpinUpSeconds` / `fFanSpinDownSeconds` | `1.5` / `4.0` | time constants of the fan: about the time to reach 2/3 of full speed / to lose 2/3 of its speed |
| `fHysteresis` | `0.05` | tolerance before switching |
| `fMinOnSeconds` / `fMinOffSeconds` | `1.0` / `1.0` | shortest on / off time |
| `bWriteCsv` | `0` | `1`: writes `ImmersiveWindsSim.csv` next to the log (time, base, motion, target, fan, relay every 50 ms) for tuning |

Tuning: start with the spin up and spin down times of your fan (switch it on and off and count). With `bLoggingEnabled=1` the log shows the base wind and the wind of the game every 3 seconds.

## Http switch

Set `sSwitchType=http` and the two urls. Any answer with status 2xx counts as success. Examples:

| Device | `sOnUrl` | `sOffUrl` |
|---|---|---|
| Shelly Gen1 | `http://<ip>/relay/0?turn=on` | `http://<ip>/relay/0?turn=off` |
| Shelly Gen2+ | `http://<ip>/rpc/Switch.Set?id=0&on=true` | `http://<ip>/rpc/Switch.Set?id=0&on=false` |
| Tasmota | `http://<ip>/cm?cmnd=Power%20On` | `http://<ip>/cm?cmnd=Power%20Off` |
| Home Assistant webhook (`sHttpMethod=POST`) | `http://<ha>:8123/api/webhook/<on id>` | `http://<ha>:8123/api/webhook/<off id>` |

## Immersive Heat (heat lamp)

A second, independent SKSE plugin, `ImmersiveHeat.dll`, switches one or two more smart plugs, e.g. with an infrared heat lamp, while the player is near a heat source. It has two heat sources:

- **Sunlight**: outside (or in an interior marked as outdoor), between `fDayStartHour` and `fDayEndHour`, with a sun glare of the weather above `fMinSunGlare` and the light level at the player above `fMinLightLevel`.
- **Fire**:
  - hit by fire (fire spells, dragon breath, fire enchanted weapons) or any fire damage effect applied to the player (traps, hazards, cloak spells) switches on right away, from the game event, and keeps it on for at least `fFireHitSeconds`; it stays on while fire damage effects are active on the player (`bFireHits`)
  - while the player charges a fire spell; casting one switches on right away (`bOwnFireSpells`)
  - a reference within `fFireRadius` whose model path contains a word of `sFireModels` and none of `sFireModelsExclude` (campfires, braziers, hearths, forges, lava, ...) (`bNearFire`). This is a heuristic: with `bLoggingEnabled=1` the log lists every model it counts as fire, which helps to tune the two lists.

**One device or two**: `iSunSwitch` and `iFireSwitch` choose the switch of each source (`1` or `2`, `0` turns the source off). With both on `1` (the default) one device heats for sunlight and for fire. With `iFireSwitch=2`, fire gets its own device. Only switches a source points at are searched and switched.

Like the fan it is off while the game is in the background (SE/AE), when the game stands still, during menus that pause the game (`bNoHeatInMenu`) and when the game exits.

Installation: copy the DLL to `Data/SKSE/Plugins`. TP-Link: name the plugs as in `sPlugName` of their switch (default **`ImmersiveHeatPlug1`** and **`ImmersiveHeatPlug2`**), or set the switch to http. No ESP is needed. Settings are written to `immersiveHeat.ini` next to `immersiveWinds.ini`, the log is `ImmersiveHeat.log`.

Upgrading from the test builds of *Immersive Sun*: delete `ImmersiveSunPluginScript.dll`, otherwise both plugins run.

| Section | Key | Default |
|---|---|---|
| `[GeneralModConfig]` | `bUseUdp`, `bLoggingEnabled` | `1`, `0` (as for the fan, for both TP-Link switches) |
| | `bNoHeatInMenu` | `1` |
| `[Switch1]`, `[Switch2]` | `sSwitchType` | `tplink` or `http` |
| | `sPlugName` | `ImmersiveHeatPlug1`, `ImmersiveHeatPlug2` (TP-Link name in the Kasa app) |
| | `sHttpMethod`, `sOnUrl`, `sOffUrl` | as for the fan's [http switch](#http-switch) |
| `[Internal]` | `sSwitch1Ip`, `sSwitch2Ip` | ip of the TP-Link plugs, found and written by the plugin |
| `[HeatSources]` | `iSunSwitch`, `iFireSwitch` | `1`, `1` |
| | `bFireHits`, `fFireHitSeconds` | `1`, `3` |
| | `bOwnFireSpells` | `1` |
| | `bNearFire`, `fFireRadius` | `1`, `350` (game units, about 5 m) |
| | `sFireModels` | `fire,flame,brazier,forge,smelter,lava,embers,hearth` |
| | `sFireModelsExclude` | `firewood,fireplace,nofire,unlit` |
| | `bSunlight`, `fMinSunGlare`, `fMinLightLevel` | `1`, `0.4` (0-1), `138` (`0` disables the light check) |
| | `fDayStartHour`, `fDayEndHour` | `7.2`, `17.76` |
| | `fLightLevelHysteresis` | `4`: in the sun, the light level has to drop this much below `fMinLightLevel` to count as shadow, so the plug does not flicker at the edge of a shadow |
| | `fCheckIntervalSeconds` | `0.1`: time between two checks (the search for fire nearby runs once per second) |

## Building

Needs Visual Studio 2022 (C++ workload), vcpkg (`VCPKG_ROOT`) and the git submodules.

```powershell
git clone --recursive https://github.com/SlowSoapSoak/SkyrimVR-ImmersiveAutomation
.\build.ps1    # builds and copies DLL + ESP into the Skyrim SE Data folder
```

Or by hand: `cmake --preset release` and `cmake --build --preset release`; set `SKYRIM_DATA_DIR` to deploy into a game folder.

Every pull request is built by GitHub Actions; the DLL is attached to the run as artifact.

## History

Up to version 1.2.0 both plugins were SKSEVR-only Visual Studio projects in `IW_V3/`, built against a copy of the SKSE sources; *Immersive Sun* got its data from a Papyrus script. Immersive Winds now lives in `src/`, Immersive Sun became *Immersive Heat* in `heat/`, both build with CommonLibSSE-NG. The old tree is no longer part of the repository; it is still in the git history, e.g. at commit `9c04d2f`.
