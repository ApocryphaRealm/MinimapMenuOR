#pragma once

// ============================================================================================================
// The minimap draws the local map itself, all the time, around the player (the owner, 2026-09-29: "a toggle that forces
// the minimap to render at all times instead of only when the local map is called in the regular map. But you'll have
// to keep it local to the area the player inhabits and not like the entire map"). [Map] bAlwaysDrawLocalMap.
//
// It borrows the game's own local-map pipeline for one frame at a time (plan ANALYSIS-4438-AND-LOCAL-MAP.md, "Refresh"):
// the two capture components on the player controller (LocalMapSceneDepthCaptureComponent / ...BaseColor...) are saved,
// pointed straight down over the player covering fCaptureWidthMetres, CaptureScene() is called on each, the game's
// own Sobel material (M_LocalMapSobelEffect) is drawn into RT_LocalMapSecondPass with KismetRenderingLibrary::
// DrawMaterialToRenderTarget, and everything is put back the same frame. The minimap shows the result through its own
// MaterialInstanceDynamic of the game's M_LocalMapUI (IsExterior set, as the game's own map page sets it). Indoors the
// camera sits fInteriorCutMetres above the player's feet, so ceilings and upper floors are above it and out of the
// picture. A new capture only when the player has moved a quarter of the width, changed cell, or turned the toggle on.
// Gameplay only (menuMode == 1); the game's own Map screen captures for itself when it is opened.
// ============================================================================================================

namespace capture
{
	void Tick(bool a_gameplay);   // game thread, every frame

	struct Record
	{
		bool         valid = false;
		UE::UObject* material = nullptr;   // our MID of M_LocalMapUI
		double       cx = 0, cy = 0;       // the capture's centre, Unreal world cm
		double       width = 0;            // cm across
	};
	Record Current();
	void   Invalidate();   // capture again on the next tick (a setting changed)
	json   State();
}
