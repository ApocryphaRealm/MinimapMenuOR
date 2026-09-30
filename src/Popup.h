#pragma once

// ============================================================================================================
// The game's location pop-up follows the minimap's corner (the owner, 2026-09-29: "add a setting in the minimap that
// links the two together so that whenever you switch which corner it's pinned to, the widget also switches its
// position, just like with DEM"). The widget is /Game/UI/Modern/HUD/WBP_ModernHud_Area (the area banner - RegionText,
// AreaDiscoveredText, TrespassingText), built once with the HUD inside WBP_ModernTopStats' vertical box: nothing on
// its path is a CanvasPanelSlot, so it is moved by its RENDER TRANSLATION (as HUD Position Manager OR moves it) -
// centred on the minimap, just below it at a top corner and just above it at a bottom corner (DEM's
// ApplyTitlePosition). Its place is read back from its cached geometry each pass, so the translation converges on the
// target whether or not the cached geometry already includes the translation.
//
// HUD Position Manager OR moves the same widget as its "Location" element; while this link is on the minimap's export
// MinimapMenu_OwnsLocationPopup() returns true and HPM stands down on that element (agreed with the agent that owns HPM).
// ============================================================================================================

namespace popup
{
	struct Rect
	{
		double x = 0, y = 0, w = 0, h = 0;   // viewport units (DPI-scaled), top left
		bool   valid = false;
		bool   anchoredTop = true;
	};

	// game thread; a_active false puts the game's own place back (once)
	void Tick(const Rect& a_map, bool a_active, double a_gapUnits);
	json State();
}
