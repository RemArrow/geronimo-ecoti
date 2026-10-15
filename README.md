# ECOTI Thermal Fusion for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that adds thermal-fusion night vision to the
gear menu's **Night Vision** list.

- **"GPNVG 18 + ECOTI" and "PVS 31 + ECOTI"**, one for every phosphor (Green, White Phosphor, White
  Filmless). This is the AN/PAS-29B Enhanced Clip-On Thermal Imager (Safran Optics 1) clipped onto the
  goggle's bridge. It folds up and down with the goggle. With the goggle **down and on**, anything warm-blooded
  is fused into the picture as white heat: other players, AI, your own hands, and bodies, which cool off over a
  few minutes.
- **"Panoramic Fusion"**, the GPNVG-18's successor. The thermal core is built into the bridge, so there is no
  clip-on. It has the same heat, and it pairs with ATAK to float a HUD in the picture:
  - a **compass tape** with the heading, cardinal points and the bearing of every marker;
  - **team markers** on every other player, with their name and range;
  - **navigation waypoints**: extraction zones, the mission objective (when you host) and your own waypoint.
    To drop your waypoint, **hold the left thumbstick in** (0.6 s) while looking at the spot. Do the same while
    looking at the sky to clear it. Markers out of view show as an arrow at the edge.
  - along the bottom: the time, the ATAK link and the thermal mode.

Everything else is the game's own goggle (its actor, flip lever, power and phosphor), so it flips, powers and
looks like the stock one. Nothing in the game is replaced.

## How it looks through the tubes

The heat is the thermal channel laid over the tubes' picture. The game's night-vision post-process turns
everything into the phosphor's colour, and a warm body comes out as a white-hot shape. Walls and cover still
hide people: thermal does not see through walls. The HUD sits in the picture at up to 3 m. When something
closer is in the way it moves in, at the same apparent size, so it never sinks into a wall.

## Settings

`GML\config\com.remarrow.ecoti.cfg`:

| | |
|---|---|
| `[Gear] ECOTI`, `Fusion` | which entries appear (next launch) |
| `[Gear] FusionPhosphor` | the Panoramic Fusion's tubes: `Green`, `WhitePhos` (default) or `WhiteFilmless` |
| `[Thermal] Enabled` | heat on or off |
| `[Thermal] Heat`, `Fusion` | how hot people look, and how much of the thermal picture covers them |
| `[Thermal] CoolMinutes` | how long a body takes to cool off (4) |
| `[Thermal] SelfHeat` | your own hands and arms are warm too |
| `[HUD] Enabled`, `FOV`, `Distance`, `Brightness` | the HUD itself |
| `[HUD] NorthYaw` | which world direction is north (0: the map's up, as on the ATAK map) |
| `[HUD] CompassSpan` | degrees the compass tape shows |
| `[HUD] RequireATAK` | team markers and waypoints only while you carry an ATAK |
| `[HUD] TeamMarkers`, `TeamRange`, `Waypoints`, `WaypointHold` | markers and the waypoint button |

## How it works

1. **Gear entries.** The game builds its gear lists, then each entry is added as a new
   `OutfitItem_NightVision` with its stock goggle's actor, lever, helmet mesh, icon and post-process. Each one
   sits next to its stock goggle.
2. **Putting one on.** A gear tile's press goes through `ProcessEvent`. For one of ours the tile carries the
   stock goggle for that moment, so the menu puts on a real asset through the game's own path. Straight
   after, the stock goggle goes on with the colour name **`ECOTI`** or **`FusionHUD`**. That pair is what
   the outfit holds, so it saves, loads and replicates like any gear. Without this plugin such a save loads
   the stock goggle. An entry put on some other way is converted the same way within a quarter second. No
   Blueprint-to-Blueprint hook is used, so this plugin doesn't collide with FPV Drone's.
3. **On the helmet.** The clip-on and the fusion core are boxes and cylinders built from code: polymer, a
   germanium lens, rubber buttons and the battery cap. They hang on the goggle's flip lever, placed from the
   housing's own bounds on top of the bridge. Other players with the plugin see them on your goggle.
4. **Heat.** While the goggle is down and powered (`NVGDevice.IsPowered`), every drawn skeletal mesh of every
   character gets a translucent unlit overlay. Each AI animates an invisible body under the outfit you see, so
   only the outfit glows. The engine's glTF material multiplies by vertex colours, which the game paints, so the
   heat is made far brighter than white. Every patch then saturates in the monochrome picture. A dead body gets
   its own copy that fades.
5. **HUD.** A quad attached to the VR camera, seen only by you, shows a render target that is redrawn every
   frame. Unreal's canvas text leaves alpha at 0 in a see-through target, so the text uses a Consolas Bold
   atlas made with GDI at startup and drawn as masked glyphs. Markers are projected through the camera, and
   five traces keep the plane in front of walls.

## Install

Needs [GML 2.2.0 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). The easiest way is
[GML Manager](https://github.com/RemArrow/geronimo-mod-loader#gml-manager): ECOTI Thermal Fusion is in its Browse tab. By hand: close
the game, download `ECOTI-<version>.zip` from [Releases](https://github.com/RemArrow/geronimo-ecoti/releases) and
extract it into `...GERONIMOGeronimoBinariesWin64`. That adds `GMLplugins$d`. To remove the
plugin, delete that folder.

## Build and install

```bat
build.bat install
```

Needs Visual Studio 2022 and the GML SDK (`..\ModLoader\include`). There are no art files. To turn it off,
put a `disabled.txt` in `GML\plugins\ECOTI` (or use GML's plugin list).

## Tested

`tests\ECOTIDev` (in game, Training Grounds, no headset) passes all of these:
- the 7 entries;
- picking one from a tile;
- a runtime entry converted;
- both clip-on fits;
- heat on, off, and cooling after death, at night with an AI in front;
- the HUD's compass, waypoint and status;
- an outfit saved and loaded (including the game leaving the old goggle's actor on the helmet, which the
  plugin rebuilds).

**Not yet tried:**
- in a headset: the HUD's distance and stereo, how bright the heat is through the real lens mask, and the
  left-thumbstick waypoint;
- in multiplayer: team markers, and other players seeing your clip-on.

Plugin by RemArrow. Not affiliated with or endorsed by Safran, L3Harris, the US Army or Dark Matter Studios.
