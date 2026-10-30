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
2. Optional: `ImmersiveWindsVR.esp` adds the *Immersive Winds Call* shout. The fan control works without it.
3. TP-Link: name the plug **`ImmersiveWindsPlug`** in the Kasa app. The plugin finds it in the local network and remembers its ip.
   http: see [Http switch](#http-switch).

On the first start the plugin writes all settings with their defaults to
`Documents/My Games/<Skyrim Special Edition|Skyrim VR>/SKSE/immersiveWinds.ini`.
The log is `ImmersiveWindsPluginScript.log` in the same folder (details with `bLoggingEnabled=1`).

## Settings (`immersiveWinds.ini`)

Missing keys are added with their default value, existing values are never overwritten.

### `[GeneralModConfig]`

| Key | Default | |
|---|---|---|
| `sSwitchType` | `tplink` | `tplink` or `http` |
| `bUseUdp` | `1` | TP-Link: send the commands with udp (`1`) or tcp (`0`) |
| `bIntermittentModeEnabled` | `1` | `1`: levels 0-6 with on/off patterns. `0`: simple mode, fan on outside, off inside |
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

On/off pattern of the levels 1-6 in seconds: `fIntermittentL<n>On`, `fIntermittentL<n>Off`, plus a random time of up to `fIntermittentL<n>RandomAddOn` / `RandomAddOff`. `On` or `Off` of `0` keeps the plug always off / on in that level. Level 0 is always off.

## Http switch

Set `sSwitchType=http` and the two urls. Any answer with status 2xx counts as success. Examples:

| Device | `sOnUrl` | `sOffUrl` |
|---|---|---|
| Shelly Gen1 | `http://<ip>/relay/0?turn=on` | `http://<ip>/relay/0?turn=off` |
| Shelly Gen2+ | `http://<ip>/rpc/Switch.Set?id=0&on=true` | `http://<ip>/rpc/Switch.Set?id=0&on=false` |
| Tasmota | `http://<ip>/cm?cmnd=Power%20On` | `http://<ip>/cm?cmnd=Power%20Off` |
| Home Assistant webhook (`sHttpMethod=POST`) | `http://<ha>:8123/api/webhook/<on id>` | `http://<ha>:8123/api/webhook/<off id>` |

## Building

Needs Visual Studio 2022 (C++ workload), vcpkg (`VCPKG_ROOT`) and the git submodules.

```powershell
git clone --recursive https://github.com/SlowSoapSoak/SkyrimVR-ImmersiveAutomation
.\build.ps1    # builds and copies DLL + ESP into the Skyrim SE Data folder
```

Or by hand: `cmake --preset release` and `cmake --build --preset release`; set `SKYRIM_DATA_DIR` to deploy into a game folder.

Every pull request is built by GitHub Actions; the DLL is attached to the run as artifact.

## History

Up to version 1.2.0 the plugin was a SKSEVR-only Visual Studio project in `IW_V3/` (with the SKSE sources and an unfinished *Immersive Sun* plugin for a second plug). That tree is no longer part of the repository; it is still in the git history, e.g. at commit `9c04d2f`.
