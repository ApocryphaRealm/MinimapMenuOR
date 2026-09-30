# Minimap Menu - changelog

Written as changes happen, not reconstructed afterwards (rule 61). A version number is issued by the version gate only
once a build is seen working in game (rule 48); until then entries sit under "Unreleased".

## Unreleased - 2026-09-29 - untested (first test build)

The owner, 2026-09-29: "start designing a minimap mod with an amf page for oblivion and use the same positioning logic
as my dem mod", the location pop-up link, the name ("Let's just call it minimap menu"), K and L, DEM's control logic
(binding and refusing keys, circle or square, tap to hide, tap to zoom, a switch for hold-to-pan), R3 on the controller
in gameplay only - and "well just build it and install when its built". Plan: 4. plans\Minimap Menu\PLAN.md.

### Added
- A minimap in the game's own HUD: a runtime UMG widget kept on the viewport and shown / hidden (logic library 7699),
  hidden while any menu is open.
- Dragon's Eye Minimap's positioning: four corners (top right by default), one offset pair per corner (+x right, +y
  down, 0 flush with the edges), the size capped at a quarter of the screen, no on-screen clamp.
- Square or circle (the circle is the brush drawn as a rounded box with the half-height radius; markers stay inside it).
- The map picture: the game's parchment (T_mappaper01), or - experimental - the game's own local-map material
  (M_LocalMapUI, showing whatever the game's pause map last captured; nothing is captured by this build).
- Markers from the HUD compass' own list (VHUDMainViewModel.CompassIconMarkers): locations and doors with the game's own
  map-icon materials, quest targets in gold (held on the rim when beyond it), enemies as red dots (off by default). North
  up or turning with the camera; the player arrow in the middle.
- The game's location banner (WBP_ModernHud_Area) follows the minimap: centred under it at a top corner, above it at a
  bottom corner, by render translation (on by default). Export MinimapMenu_OwnsLocationPopup() for HUD Position Manager
  OR, which stands down on its Location element while it returns true.
- Controls (DEM's logic): K taps to hide / show and, held, pans the map with the mouse until let go (then it recentres);
  L taps between two zoom levels; a switch turns hold-to-pan off (then K acts on press); the right stick click does the
  same on the controller (read through AMF), in gameplay only. Keys are bound on the page by pressing them; the
  framework's keys, Tab, the other action's key and CCM's defaults are refused with the reason.
- AMF pages Map, Controls, Markers and Status; TestBench tools minimap.status and minimap.drive.

### Round 2 (the owner's first report, 2026-09-29)
- A frame: a brown outline (the paper-map ink, #4a3222) around the map, square or round with it. The owner: "The
  minimap needs to have some sort of frame to go around it."
- The location banner never moved in round 1: the object found was the widget blueprint's TEMPLATE (an archetype named
  plain WBP_ModernHud_Area, never laid out). Only live objects are used now (no class default object, no archetype), and
  the banner chosen is the one actually laid out. Its current render translation is read from the widget, so an offset
  another mod left on it does not skew the placement.
- The export MinimapMenu_OwnsLocationPopup() is true only while the minimap is on screen and placing the banner, so HUD
  Position Manager's layout applies whenever it is not (the primary agent's request).
- Far locations on the rim ([Markers] bFarLocationsOnRim, on by default): a location beyond the map's reach stays on the
  rim, dimmed and a little smaller, pointing the way as the compass shows it. The first reading explained the owner's
  "a blank map with a compass marker and maybe a door": 5 compass markers, 2 drawn - the three locations were 69, 92
  and 117 m away (if Distance is in centimetres), outside the 60 m reach, and only quest targets were kept on the rim.
  The same reading matched the minimap's heading to the compass (152.9) and put the two doors at 11.5 and 15.6 m.

### Round 3 - the game's own local map (2026-09-29)
- The owner: "we need to be able to see the actual lines for the local map ... it should read from the local map if
  there is a local map". The game draws its local map's wall and path lines itself (its captures, its Sobel material,
  M_LocalMapUI) and its Map screen shows them through a material instance (VModern_NavigableMapWidget
  .LocalMapMaterialDynamic). While a menu is open the minimap READS that material from the game's map page; in gameplay
  it shows the same material, placed with the game's own helper ULocalMapManager::GetLocalMapCoordinates (the player's
  point and one metre east and north of it give the map's scale and rotation - nothing assumed). Where that map does not
  cover the player (another cell, or beyond it) the parchment shows. Nothing is captured or drawn by the mod.
- [Map] iMapImage now defaults to 1 (the game's local map where there is one); the test install moves a player INI still
  holding the old default 0.
- Research: 4. plans\Minimap Menu\ANALYSIS-4438-AND-LOCAL-MAP.md (Minimap 4438's approach from its public page - its
  own capture and post-process Sobel, no markers; nothing of it is used).

### Round 4 - the local map drawn at all times (2026-09-29)
- The owner: "a toggle that forces the minimap to render at all times instead of only when the local map is called in
  the regular map. But you'll have to keep it local to the area the player inhabits and not like the entire map."
  [Map] bAlwaysDrawLocalMap (on by default): the minimap borrows the game's own local-map pipeline for one frame - the
  two capture components on the player controller are saved, pointed straight down over the player covering
  [Rendering] fCaptureWidthMetres (180 m), captured, the game's Sobel material drawn into RT_LocalMapSecondPass, and
  put back the same frame - and shows it through its own instance of M_LocalMapUI (IsExterior set). Indoors the camera
  sits fInteriorCutMetres (2.5 m) above the feet so ceilings stay out. A new capture after moving a quarter of the width
  or changing cell; only while the minimap is on screen in gameplay.
- iMapQuarterTurns / bMapMirror: corrections in case the drawn map comes out turned or mirrored (north at the top and
  east to the right is assumed from the game's CameraRotationAngles).
- The local map's size is read from the Map screen's MapImage brush (probe 5: 4096 x 4096; the page view model's
  MapSize reads 0) - for the Map-screen reading, which stays the fallback when the toggle is off.

### Round 4b - per-frame work cut the Dragon's Eye Minimap way (2026-09-29)
- The owner: "Make sure you have null pointer guards and refer to how Dragon's Eye Minimap optimized for FPS by not
  constantly calling the local map per frame. So we want to limit per frame features."
- DEM's two rendering switches carried over: [Rendering] bSkipWhileWorldSettles / iSettleMs (1500) - no new drawing of
  the map during a load or for 1.5 s after one (the first sight of the player, going in or out of doors, a jump of more
  than 50 m); the last picture stays, and ordinary travel never waits - and iRedrawIntervalMs (1000), the least time
  between two of the minimap's own captures. The frame, markers and arrow keep updating either way.
- What no longer runs every frame:
  - the 247-key scan (only while a key is being bound, from the keys held when binding starts);
  - the status copy for the page and TestBench (4 times a second);
  - the player arrow and the local-map image (their engine calls now only when a value changed);
  - the game's map-coordinate helper (3 calls once per map read, then linear arithmetic);
  - the object-array scan for the location banner (every 10 s, known banners re-measured each second, placed 5 times
    a second instead of 10);
  - the object-array scan for the Map screen's map (every 1.5 s, and not again once found until gameplay resumes);
  - the layout (twice a second); the markers (20 times a second, re-setting only what moved).
- Null guard added where the map panel is made (a failed NewObject stops the build instead of adding nothing).

### Round 5 (the owner's report, 2026-09-29)
- The local map is drawn indoors now. The owner: "The local map appears in the exterior, but not in the interior." The
  log showed the settle window re-arming itself every check indoors - it compared the player's interior flag with the
  LAST CAPTURE's, which only a capture updates, and the wait kept any capture from coming. The window has its own record.
- A reach of its own indoors, [Map] fRadiusInteriorMetres (25 m; the owner: "we might have to have a separate zoom
  setting for interiors versus exteriors").
- The location banner's text is scaled down to fit the minimap's width ([Display] bFitPopupToMinimap, fPopupScale; the
  owner: "a text size that fits well beneath the minimap").
- The banner switch is labelled "The minimap places the location banner" - off hands it to HUD Position Manager (or the
  game), as the owner asked ("release control of the location pop up to wherever the HUD position manager sets it").
- The compass can go with the minimap ([Display] bPairCompass, off by default; bFitCompassToMinimap, fCompassScale): under
  it at a top corner, above it at a bottom corner, where the banner was, and the banner after the compass (the owner:
  "an optional toggle in the minimap to pair the compass to be where the location pop up was"). A second export,
  MinimapMenu_OwnsCompass(), for HUD Position Manager's Compass element. The banner and the compass share one follower
  (Popup.cpp), which now also scales.
- Precise sliders: a keyboard or D-pad nudge moves exactly one unit of the last digit shown (include/PreciseSlider.h;
  the owner: "all of our sliders are precise sliders and they don't jump more than one numerical unit per D-pad nudge").

### Round 6 (the owner's report, 2026-09-29)
- Round 5 confirmed in game: "The minimap appears inside and outside" - the always-drawn local map works in both.
- The compass size setting works (the owner: "The compass size in the minimap menu isn't responsive"). With the fit on
  (the default), the fit capped the size: any size above the fitted scale did nothing, and the compass is so much wider
  than the minimap that this was every setting above about 0.4. The size now applies on top of the fit, so with the fit
  on, 1.00 is the minimap's width. The banner's size had the same flaw and is fixed the same way. Also in this build:
  the precise SliderInt (345b317).

### Known gaps in this build
- Untested: whether the game's capture draws the right things with the show-only list off, whether the pause map's
  local page still shows its own area after the minimap has captured (it shares the render targets), and the capture cost.
- In the circle shape the drawn map's corners show outside the round frame (no round mask yet).
- The compass list's Angle and Distance units are assumed (a compass bearing in degrees, centimetres); minimap.status
  shows raw samples so the first round can confirm them.
- While panning with the stick, the camera also turns (AMF's stick capture governs its own menu, not the game camera).
- Text is English only so far; the ten other languages are owed before release (rule 66).
