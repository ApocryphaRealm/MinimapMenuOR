#pragma once

// ============================================================================================================
// The game's own local map on the minimap (the owner, 2026-09-29: "we need to be able to see the actual lines for the
// local map ... it should read from the local map if there is a local map"). Plan: ANALYSIS-4438-AND-LOCAL-MAP.md,
// "Reading the game's local map".
//
// The game draws its local map's wall and path lines itself - two top-down captures into RT_LocalMapSceneDepth /
// BaseColor, its Sobel material into RT_LocalMapSecondPass, composed by M_LocalMapUI - and its map page shows them
// through a MaterialInstanceDynamic, VModern_NavigableMapWidget.LocalMapMaterialDynamic. Nothing here captures or
// draws anything: while a menu is open this READS the game's widget for that material (and the page view model for
// the map's size); in gameplay the minimap shows the SAME material on its own image, placed so the player's point sits
// in the middle. Where the player is on that map, and which way it faces, is measured with the game's own reflected
// helper ULocalMapManager::GetLocalMapCoordinates (the player's world point, and one metre east and north of it), so
// no unit, origin or rotation is assumed. When the player is off that map (another cell, or beyond it), the parchment
// shows instead - until the Map screen is opened there.
// ============================================================================================================

namespace localmap
{
	// game thread, every frame: a_menuOpen = menuMode != 1 (the map page can be read), a_pawn = the player's pawn
	void Tick(bool a_menuOpen);

	struct Placement
	{
		bool   ok = false;           // the game's local map covers the player: show it
		UE::UObject* material = nullptr;
		double width = 0, height = 0;    // the image's size in panel units
		double x = 0, y = 0;             // the image's top left in panel units (before rotation)
		double pivotX = 0.5, pivotY = 0.5;   // the player's point, as a fraction of the image
		double angle = 0;                // degrees, clockwise
	};

	// where the image goes: a_centreX/Y the player's point in the panel, a_pxPerCm the minimap's scale, a_upDeg the
	// compass bearing that points up (0 north-up)
	Placement Place(double a_centreX, double a_centreY, double a_pxPerCm, double a_upDeg);

	json State();
}
