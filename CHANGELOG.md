# Minimap Menu - changelog

Written as changes happen, not reconstructed afterwards (rule 61). A version number is issued by the version gate only
once a build is seen working in game (rule 48); until then entries sit under "Unreleased".

## 1.0.0 - 2026-09-30 - untested

### Release (2026-09-30)
- **Changed: no controller button by default** (the owner, at the finalize: "R3 is the target lock button, so don't map
  it to anything for now on controller"). bGamepadHideButton and iPanHoldGamepadButton ship 0; any button can be bound
  on the page.
- The page in eleven languages. MM_Key, a stray example key from a comment in Strings.h, is gone from every file.
- The package: README, LICENSE, NOTICE and THIRD_PARTY_NOTICES, keeping Dragon's Eye Minimap's MIT upstream notice.
- **Fixed: the local map's corners outside the circle** (the owner's screenshot, 2026-09-30 02:28). The panel clips to its
  square, so the map image - larger than the minimap and turned about the player - showed its square corners past the
  round frame. In the circle the image now fills the minimap exactly, shows only the part in view through the brush's
  UVRegion, and is drawn as a rounded box like the parchment; a circle is the same circle at any angle, so the turned map
  stays inside it. The square shape is unchanged.

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

### Round 7 (the owner's report, 2026-09-30)
- **Fixed: enemy dots drifting while the view turns.** The owner: "The enemy markers kind of float around the screen a
  bit when rotating".
  - Cause: the compass' HostileData holds only a distance and an angle, and that angle does not keep to the map's frame
    while the view turns. The location markers, from CompassIconMarkers, held still.
  - Fix: while the compass lists enemies, the paired pawns whose reference is in combat with the player are found. That
    is a whole object-array scan, run at most twice a second and only while the compass lists any.
  - Each enemy is then placed from its own world position, on the same rotation, quarter turns and mirror as the map
    image, so a dot stays on its spot of the map.
  - The compass' angles are the fallback when no body is found. `minimap.status` markers.hostiles_from says which was
    used.
- **Changed: the compass and the banner can grow larger.** The owner: "The compass is about a centimeter on either side
  of space before reaching the minimaps border but its maxed out". The compass widget's box is wider than its visible
  bar, so "fitted" left it short of the edges. fCompassScale and fPopupScale now go to 3.00 (from 1.50), and the hint
  says to raise the value until the widget reaches the minimap's edges.
- **Added: a press-to-bind listener for the controller button** (the owner: "should be a button binding. Listener, so
  that you can rebind it to any controller button").
  - How binding works: Bind waits until every button is let go, so the A that pressed Bind isn't taken. It then arms AMF's
    controller capture (AMF 1.0.2+), which keeps that press from the menu and the game. Any button is taken; a stick
    direction or a trigger is refused with a reason. The page shows the button's name, and Cancel or the 8-second
    timeout gives up.
  - How the button is read: in gameplay, from the XInput function the game itself imports. It is found with
    GetModuleHandle in the module the game already loaded, never LoadLibrary, and called directly rather than through
    the import slot, so Improved Wheel Menu's controller rules don't run twice. Holding the button pans with the right
    stick, or with the left stick when the button is the left stick click.
  - A framework without the capture keeps the old two-choice list. AMF.h is the 1.0.4 SDK copy.

### Round 8 (the owner, 2026-09-30)
- **Changed: the compass's size setting is now anchored to the owner's setting.** The owner: "whatever I currently have
  should be set as the true boundary for the bounds of the mini map and the compass". Their setting was fCompassScale
  2.65 with the fit on, which put the visible bar exactly across the minimap, so the bar is 1/2.65 of the compass
  widget's box. With the fit on, the bar itself is now sized to the minimap's width, up or down, and 1.00 is that width.
  The owner's INI moves from 2.65 to 1.00 at install, so the look is unchanged. The banner keeps shrink-only text
  fitting.
- **Changed: the gap can go below 0 (down to -200 px)** (the owner: "the gap should let me go closer to the mini map
  because it's currently zeroed out but there's still some space in between"). The compass's box has empty space of its
  own above the bar.

### Round 9 (the owner's report, 2026-09-30)
- The owner: "The compass gap is good now."
- **Fixed: enemy dots still swung with the camera** (the owner: "whenever I rotate the camera ... it moves the enemy
  marker all over the place along with the rotation"; they pointed to Dragon's Eye Minimap, which once had the same
  problem).
  - Cause: round 7's world-position path never ran. It matched each Unreal body to its reference by the form ID in the
    pawn's TESRefComponent, and that never matches (Camera Configuration Menu's speaker lookup showed the same). No
    enemy body was found, and every dot fell back to the compass' angles, which turn with the camera.
  - Fix: the enemies now come from the player's own detection list (HUD Position Manager's sneak ring read, probed live
    the same day): actors in combat with the player, alive. Each one's body comes through the game's pairing (its
    IVPairableItem pairing entry holds the Unreal actor).
  - The log says which path placed the enemies whenever that changes.
  - Dragon's Eye Minimap's own lesson was already in place: the markers are placed in the same update as the map
    image's rotation, never a frame behind it.

### Round 10 (2026-09-30)
- **Fixed: a crash on quitting to the main menu** (01:49:42, EXCEPTION_ACCESS_VIOLATION reading 0xF80 in
  GetViewportSize).
  - Cause: the layout read the viewport every 500 ms even behind menus, with the cached player controller as the world
    context. During a quit the controller outlives its world.
  - Fix: the layout runs only in gameplay. A dying controller (destroyed, garbage, unreachable) is refused. Every call
    that takes a world context goes through a fault-guarded ProcessEvent (Call::RunGuarded): the layout, the widget
    Create, the popup's LocalToViewport and the capture's material and draw calls. A follower that isn't active doesn't
    go looking for its widget.
  - Logic library entry and gate rule or-world-context-calls-are-guarded of the same day.

### Known gaps in this build
- Untested: whether the game's capture draws the right things with the show-only list off, whether the pause map's
  local page still shows its own area after the minimap has captured (it shares the render targets), and the capture cost.
- In the circle shape the drawn map's corners show outside the round frame (no round mask yet).
- The compass list's Angle and Distance units are assumed (a compass bearing in degrees, centimetres); minimap.status
  shows raw samples so the first round can confirm them.
- While panning with the stick, the camera also turns (AMF's stick capture governs its own menu, not the game camera).
- Text is English only so far; the ten other languages are owed before release (rule 66).
