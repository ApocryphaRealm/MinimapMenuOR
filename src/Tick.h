#pragma once

// ============================================================================================================
// The game-thread frame tick, by chaining the game executable's USER32!PeekMessageW import (the previous target is
// kept and called, so other plugins chaining the same slot work in either order). Unreal's message pump calls it
// every frame on the main thread; the callback runs at most once every 2 ms. OBSE64 has no main-loop interface.
// Taken from Weightless Menu (Tween Menu for Oblivion before it). Minimap Menu adds a message filter: while the map is
// being panned with the mouse, the raw mouse messages (WM_INPUT) are read for their movement and turned into WM_NULL,
// so the camera does not also turn (Dragon's Eye Minimap's hold-to-pan hands the mouse to the map the same way).
// ============================================================================================================

namespace tick
{
	using FrameCallback = void (*)();
	bool Install(FrameCallback a_frame);   // at OBSE's post-load, on the game's main thread

	// called on the game thread for every message PeekMessageW removes; true = swallow it (it becomes WM_NULL)
	using MessageFilter = bool (*)(MSG* a_msg);
	void SetMessageFilter(MessageFilter a_filter);
	std::uint64_t Reads();
}
