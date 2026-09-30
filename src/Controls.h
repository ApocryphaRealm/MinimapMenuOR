#pragma once

// ============================================================================================================
// Dragon's Eye Minimap's control logic (DragonsEyeMinimap-SMF source\Controls.cpp:106-160, UI.cpp:59-72, 413-470),
// carried to Oblivion Remastered (the owner, 2026-09-29):
//   * the hide key (K) - a TAP shows or hides the map on RELEASE; HELD past fHoldToPanSecs while the map shows, it
//     pans the map with the mouse until let go, and the map recentres (only while bHoldHideToPan; with it off the key
//     acts on PRESS). A runtime toggle never writes bShowOnGameStart;
//   * the zoom key (L) - a tap switches between the two zoom levels;
//   * the controller - the same tap / hold on a stick click (R3 by default), read through AMF (logic library 7152:
//     Steam Input answers XInput first, so the framework's pad reader is the one that sees it); holding pans with that
//     stick;
//   * GAMEPLAY ONLY (the owner: "In gameplay context only, not in menus") - nothing is read while a menu is open, and
//     nothing is taken from the game: the keys are only READ (the owner: a vanilla action on a default key is the
//     player's to rebind in the game's Controls page);
//   * binding keys on the page - the next key pressed is captured on the game thread; one that cannot be bound is
//     refused with its reason (the framework's own keys, Tab, another of this mod's actions, a default of another of
//     our Oblivion Remastered mods); Escape cancels.
// ============================================================================================================

namespace controls
{
	// game thread, every frame. a_gameplay: menuMode == 1 and a player; a_shown: the map is on screen
	void Tick(bool a_gameplay, bool a_shown, double a_dt);

	// what the tick decided, taken once by the minimap
	bool TakeToggleShown();
	bool TakeToggleZoom();
	bool Panning();
	std::array<double, 2> TakePan();   // pixels moved since the last take (map space: +x right, +y down)

	// the tick's message filter: raw mouse movement while panning with the mouse (true = swallowed)
	bool OnMessage(MSG* a_msg);

	// key binding from the page (any thread): 1 = hide key, 2 = zoom key
	void BeginCapture(int a_target);
	void CancelCapture();
	int  Capturing();
	std::string LastCaptureMessage();   // the reason a key was refused, or what was bound (English; the page translates its own)
	int  LastRefusal();                 // -1 none, else an index into RefusalKeys()
	const char* const* RefusalKeys();   // TR keys, parallel to RefusalEnglish()
	const char* const* RefusalEnglish();

	std::string KeyName(std::int32_t a_scan);   // "K", "[", "Num 5" ... (the keyboard's own name for the key)

	void NotePageDrawn();   // the AMF page drew this frame: the keys stay quiet while the framework's menu is open
	json State();
}
