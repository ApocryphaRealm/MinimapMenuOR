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

### Known gaps in this build
- The map picture does not show the terrain around the player yet (the capture of route A waits for the M0 probes).
- The compass list's Angle and Distance units are assumed (a compass bearing in degrees, centimetres); minimap.status
  shows raw samples so the first round can confirm them.
- While panning with the stick, the camera also turns (AMF's stick capture governs its own menu, not the game camera).
- Text is English only so far; the ten other languages are owed before release (rule 66).
