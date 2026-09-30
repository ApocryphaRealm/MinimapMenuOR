#pragma once

// ============================================================================================================
// HUD widgets that follow the minimap's corner, moved by their RENDER TRANSLATION (nothing on their path is a
// CanvasPanelSlot - as HUD Position Manager OR moves them):
//   * the location banner, /Game/UI/Modern/HUD/WBP_ModernHud_Area (the owner, 2026-09-29: "add a setting in the minimap
//     that links the two together so that whenever you switch which corner it's pinned to, the widget also switches its
//     position, just like with DEM") - with its text scaled to fit under the minimap ("a text size that fits well
//     beneath the minimap");
//   * optionally the compass, WBP_ModernHud_Compass ("an optional toggle in the minimap to pair the compass to be where
//     the location pop up was ... just below the minimap or just above it if it's in the bottom of the screen") - then
//     the banner goes after the compass.
// Each is centred on the minimap, just below it at a top corner and just above it at a bottom corner, and scaled down to
// the minimap's width when wider. Only live, laid-out instances are used (the template is never laid out - round 1).
// HUD Position Manager OR stands down on each while the minimap's export for it returns true.
// ============================================================================================================

namespace popup
{
	struct Rect
	{
		double x = 0, y = 0, w = 0, h = 0;   // viewport units (DPI-scaled), top left
		bool   valid = false;
		bool   anchoredTop = true;
	};

	enum class Widget { kBanner, kCompass };

	// game thread. a_after: the rectangle to stack after (the minimap, or the compass placed before). a_active false puts
	// the game's own place back (once). a_fit: scale down to a_after's width; a_scale: a further scale. Returns the
	// widget's placed rectangle (invalid when it is not placed right now).
	Rect Tick(Widget a_which, const Rect& a_after, bool a_active, double a_gapUnits, bool a_fit, double a_scale);
	bool Placing(Widget a_which);   // found, laid out and placed by this mod (what the exports tell HUD Position Manager)
	json State();
}
