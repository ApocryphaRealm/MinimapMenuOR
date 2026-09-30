Minimap Menu
============
Version 1.0.0

An original, GPL-3.0-or-later OBSE64 plugin for The Elder Scrolls IV: Oblivion Remastered. A minimap in
the game's own HUD, drawn from the game's own local map, configured on an in-game settings page of the
Apocrypha Menu Framework.

WHAT IT DOES
------------
  * A minimap in a screen corner of your choice, square or round, with a frame, its own offset
    per corner and a size capped at a quarter of the screen.
  * The game's own local map - its map capture with the wall and edge lines - drawn around you at
    all times, indoors and outdoors (it can also show only what the Map screen's local map has).
  * Markers from the HUD compass: locations, doors, quest targets and, optionally, enemies. Enemies
    are placed from where they stand, so they stay put while you turn.
  * The map turns with the camera (or stays north-up).
  * The location banner and the compass can follow the minimap's corner, sized to its width.
  * Light on the frame rate: the map is redrawn about once a second at most, never while the
    world is loading, and markers are only moved when they change.

KEYS
----
  * K - tap to hide or show the minimap; hold to pan it with the mouse (it recentres when let go).
  * L - tap to switch between the two zoom levels.
  * A controller button can do the same tap and hold: none is bound out of the box (the right stick
    click is the target lock); bind any button on the page.
  * Gameplay only: nothing happens while a menu is open.
  * All keys are rebindable on the page. A vanilla action on the same key is yours to rebind in the
    game's own Controls page.

REQUIREMENTS
------------
  * The Elder Scrolls IV: Oblivion Remastered (Steam, runtime 1.512.105)
  * OBSE64 (Oblivion Script Extender 64)
  * Address Library for OBSE Plugins
  * Apocrypha Menu Framework for Oblivion Remastered 1.0.4 or later - for the settings page
    (without it the INI still applies)
  * Optional: HUD Position Manager - it leaves the location banner and the compass to the
    minimap while the minimap places them

INSTALLING
----------
The plugin goes beside the game executable, in
OblivionRemastered\Binaries\Win64\OBSE\Plugins\. With Mod Organizer 2 that means the Root
folder layout (Root Builder); launch the game through OBSE64.

FILES
-----
  * OBSE/Plugins/MinimapMenu.dll - the plugin (MinimapMenu.pdb, its debug symbols, beside it)
  * OBSE/Plugins/MinimapMenu.ini - every setting of the page, and the log level
  * OBSE/Plugins/ApocryphaMenuFramework/Translations/MinimapMenu_*.txt - the page in eleven
    languages
  * The log is Documents/My Games/Oblivion Remastered/OBSE/Logs/MinimapMenu.log. It is written at
    info; set uLogLevel=0 in the INI for everything when reporting a problem.

CREDIT AND LICENCE
------------------
The positioning and the controls (the corners, the size cap, tap to hide, hold to pan, the zoom key,
the key binding with its refusals) are carried over from Dragon's Eye Minimap for Skyrim - alexsylex's
original and the ApocryphaRealm update of it. The local map is the game's own: its capture and its
edge-detection material, no art of anyone else's. GPL-3.0-or-later (LICENSE, NOTICE.md); the
components it links and their notices: THIRD_PARTY_NOTICES.md.
