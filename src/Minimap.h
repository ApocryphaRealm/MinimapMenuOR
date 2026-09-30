#pragma once

// ============================================================================================================
// Minimap Menu's widget (game thread). Plan: 4. plans\Minimap Menu\PLAN.md.
//
// One runtime-built UserWidget (a WidgetTree with a CanvasPanel root, as CCM's marker is built) kept on the viewport
// for the whole session and only shown and hidden - taken off the viewport, nothing holds it and the garbage collector
// frees it (logic library 7699). On it: the map panel (a CanvasPanel clipped to its bounds) placed by Dragon's Eye
// Minimap's positioning - a corner anchor with one offset pair per corner, +x always right and +y always down, (0, 0)
// flush with the screen edges, the size capped at a quarter of the screen, no on-screen clamp (plan section 4). In the
// panel: the map image (the game's parchment, or - experimental - the game's own local-map material), the markers the
// HUD compass already knows (VHUDMainViewModel.CompassIconMarkers / HostileData: a polar plot around the player, the
// game's own map-icon materials), and the player arrow.
// ============================================================================================================

namespace minimap
{
	void Tick();   // game thread, every frame (the PeekMessageW tick)

	enum class Action { kToggleShown, kToggleZoom, kRecentre, kRebuild };
	void Queue(Action a_action);   // any thread; applied on the next tick

	bool OwnsLocationPopup();   // any thread: the minimap is placing the location banner (HUD Position Manager OR asks)
	bool OwnsCompass();         // any thread: the minimap is placing the compass (bPairCompass)
	json State();               // any thread
}
