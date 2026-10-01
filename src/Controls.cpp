#include "Controls.h"

#include "AMF.h"
#include "Settings.h"

#include <Xinput.h>   // types and constants only - nothing is linked or loaded

namespace controls
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// the tap / hold state of one button (DEM's scheme: the decision is made on release unless the hold took over)
		struct Hold
		{
			bool      down = false;
			ULONGLONG since = 0;
			bool      panning = false;
			bool      actedOnPress = false;
		};

		Hold g_key, g_pad;
		bool g_zoomDown = false;
		std::array<bool, 256> g_vkDown{};   // for the capture's "newly pressed" test

		std::atomic<bool> g_toggleShown{ false }, g_toggleZoom{ false }, g_panning{ false }, g_mousePanning{ false };
		std::mutex        g_panLock;
		double            g_panX = 0.0, g_panY = 0.0;
		std::atomic<std::int64_t> g_pageDrawnAt{ 0 };

		// key capture (page -> game thread)
		std::atomic<int> g_capture{ 0 };
		std::atomic<int> g_refusal{ -1 };
		std::mutex       g_msgLock;
		std::string      g_message;

		constexpr const char* kRefusalKeys[] = { "ResFramework", "ResTab", "ResOtherAction", "ResOurMod", "ResMouse" };
		constexpr const char* kRefusalEnglish[] = {
			"the menu framework uses it",
			"Tab opens the game's quick keys",
			"another of this mod's actions uses it",
			"another of our mods uses it by default",
			"mouse buttons cannot be bound",
		};

		// defaults our OTHER Oblivion Remastered mods ship (.MD\DEFAULT-KEYS.md, per game): refused so two of our mods
		// never share a key the player cannot see in the game's own Controls page
		struct OurKey { std::int32_t scan; const char* mod; };
		constexpr OurKey kOurMods[] = { { 26, "Camera Configuration Menu" }, { 27, "Camera Configuration Menu" } };

		// The controller's buttons, from the XInput function the game itself imports - found in the module the game already
		// loaded (GetModuleHandle, never LoadLibrary: a plugin that loads an XInput DLL kills the controller, logic library
		// 7178). Called directly, not through the game's import slot, so no other plugin's controller rules run twice.
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
		XInputGetState_t PadReader()
		{
			static XInputGetState_t s_fn = nullptr;
			static bool             s_looked = false;
			if (!s_looked) {
				s_looked = true;
				for (const wchar_t* dll : { L"XINPUT1_3.dll", L"xinput1_4.dll", L"XINPUT9_1_0.dll" }) {
					if (HMODULE m = ::GetModuleHandleW(dll)) {
						s_fn = reinterpret_cast<XInputGetState_t>(::GetProcAddress(m, "XInputGetState"));
						if (s_fn) {
							logger::info("controls: the controller is read from the game's own XInput module ({})", dll[6] == L'1' && dll[8] == L'3' ? "XINPUT1_3" : "another XInput");
							break;
						}
					}
				}
				if (!s_fn) logger::warn("controls: no XInput module is loaded by the game - only a stick click can be the controller button");
			}
			return s_fn;
		}

		// the buttons held now (0 when no pad or no reader); a_ok false when the pad cannot be read at all
		WORD PadButtons(bool* a_ok = nullptr)
		{
			XINPUT_STATE st{};
			auto*        fn = PadReader();
			const bool   ok = fn && fn(0, &st) == ERROR_SUCCESS;
			if (a_ok) *a_ok = ok;
			return ok ? st.Gamepad.wButtons : 0;
		}

		// the pad button capture (the owner, 2026-09-30: "The controller button for hold to pan on the minimap should be a
		// button binding. Listener, so that you can rebind it to any controller button"): 1 = waiting for every button to
		// be let go (the A that pressed Bind is not the new button), 2 = AMF's capture armed
		std::atomic<int> g_padCapture{ 0 };

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		}

		bool GameInFront()
		{
			DWORD pid = 0;
			const HWND fg = ::GetForegroundWindow();
			return fg && ::GetWindowThreadProcessId(fg, &pid) && pid == ::GetCurrentProcessId();
		}

		// DirectInput scan code <-> virtual key (an extended key's DIK code is 0x80 | its scan byte)
		UINT VkFromScan(std::int32_t a_scan)
		{
			if (a_scan <= 0 || a_scan > 255) return 0;
			const UINT sc = a_scan >= 0x80 ? (0xE000u | static_cast<UINT>(a_scan & 0x7F)) : static_cast<UINT>(a_scan);
			return ::MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
		}

		std::int32_t ScanFromVk(UINT a_vk)
		{
			const UINT sc = ::MapVirtualKeyW(a_vk, MAPVK_VK_TO_VSC_EX);
			if (!sc) return 0;
			return (sc & 0xFF00) == 0xE000 ? static_cast<std::int32_t>(0x80 | (sc & 0x7F)) : static_cast<std::int32_t>(sc & 0xFF);
		}

		bool Down(std::int32_t a_scan)
		{
			const UINT vk = VkFromScan(a_scan);
			return vk && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
		}

		void Message(std::string a_m, int a_refusal)
		{
			g_refusal.store(a_refusal);
			std::scoped_lock l(g_msgLock);
			g_message = std::move(a_m);
		}

		// -1 = the key may be bound; otherwise the refusal's index. a_target: 1 hide, 2 zoom
		int Refusal(std::int32_t a_scan, int a_target, std::string& a_who)
		{
			std::int32_t reserved[32]{};
			const auto   n = AMF::ReservedKeys(reserved, 32);
			for (std::uint32_t i = 0; i < n && i < 32; ++i) {
				if (reserved[i] == a_scan) return 0;
			}
			if (a_scan == 15) return 1;   // Tab: the game's quick keys radial (and Tween Menu's)
			const auto& s = settings::Get();
			if ((a_target == 1 && a_scan == s.zoomToggleKey) || (a_target == 2 && a_scan == s.hideKey)) return 2;
			for (const auto& k : kOurMods) {
				if (k.scan == a_scan) {
					a_who = k.mod;
					return 3;
				}
			}
			return -1;
		}

		// the page asked for the next key: the first key newly pressed on this frame (game thread)
		void RunCapture()
		{
			const int target = g_capture.load();
			for (UINT vk = 0x08; vk < 0xFF; ++vk) {
				const bool down = (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
				const bool edge = down && !g_vkDown[vk];
				g_vkDown[vk] = down;
				if (!edge || target == 0) continue;
				if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) continue;
				if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;   // the L/R variants below carry them
				const auto scan = ScanFromVk(vk);
				if (scan <= 0) continue;
				if (scan == 1) {   // Escape: cancel
					g_capture.store(0);
					Message("binding cancelled", -1);
					logger::info("controls: key capture cancelled");
					return;
				}
				std::string who;
				const int   why = Refusal(scan, target, who);
				if (why >= 0) {
					Message(std::format("{} cannot be bound: {}{}", KeyName(scan), kRefusalEnglish[why], who.empty() ? "" : " (" + who + ")"), why);
					logger::info("controls: {} refused for the {} key - {}{}", KeyName(scan), target == 1 ? "hide" : "zoom", kRefusalEnglish[why],
						who.empty() ? "" : " (" + who + ")");
					return;   // still capturing: the next key is tried
				}
				auto& v = settings::Get();
				(target == 1 ? v.hideKey : v.zoomToggleKey) = scan;
				settings::Save();
				g_capture.store(0);
				Message(std::format("{} bound", KeyName(scan)), -1);
				logger::info("controls: the {} key is now {} (scan {})", target == 1 ? "hide" : "zoom", KeyName(scan), scan);
				return;
			}
		}

		// one button's tap / hold (DEM Controls.cpp:106-140). a_down: held now. Returns true while it pans.
		void Step(Hold& a_h, bool a_down, bool a_shown, const char* a_name)
		{
			const auto&     s = settings::Get();
			const ULONGLONG now = GetTickCount64();
			const auto      threshold = static_cast<ULONGLONG>(std::max(0.05f, s.holdToPanSecs) * 1000.0f);
			if (a_down && !a_h.down) {
				a_h.down = true;
				a_h.since = now;
				a_h.actedOnPress = false;
				if (!s.holdHideToPan) {
					a_h.actedOnPress = true;   // nothing to wait for: the toggle is the press
					g_toggleShown.store(true);
					logger::debug("controls: {} pressed - toggle (hold-to-pan is off)", a_name);
				}
			} else if (a_down && a_h.down) {
				if (s.holdHideToPan && !a_h.panning && a_shown && now - a_h.since >= threshold) {
					a_h.panning = true;
					logger::debug("controls: {} held {} ms - panning", a_name, now - a_h.since);
				}
			} else if (!a_down && a_h.down) {
				a_h.down = false;
				if (a_h.panning) {
					a_h.panning = false;
					logger::debug("controls: {} released - panning ends, the map recentres", a_name);
				} else if (!a_h.actedOnPress && now - a_h.since < threshold) {
					g_toggleShown.store(true);
					logger::debug("controls: {} tapped ({} ms) - toggle", a_name, now - a_h.since);
				}
			}
		}
	}

	void Tick(bool a_gameplay, bool a_shown, double a_dt)
	{
		const auto& s = settings::Get();
		if (g_capture.load() != 0) {
			RunCapture();   // the page is open (a menu): the capture runs; nothing else does
			g_key = {}, g_pad = {};
			g_panning.store(false);
			g_mousePanning.store(false);
			return;
		}
		// the framework's window counts as a menu on every page, not only this mod's (the owner, 2026-10-01: with the
		// controller button set to D-pad left, "it's hiding the minimap on and off whenever I use the D-pad in AMF") - its
		// window is drawn over gameplay (menuMode stays 1), and this button is read straight from XInput, before the
		// framework's pad gate. AMF::IsMenuOpen is 1.0.5+; older frameworks fall back to this page's own drawn time.
		const bool quiet = !a_gameplay || !GameInFront() || AMF::IsMenuOpen() || NowMs() - g_pageDrawnAt.load() < 250;
		if (quiet) {
			// a menu opened mid-press: nothing is decided, panning ends (the owner: gameplay only)
			if (g_key.panning || g_pad.panning) logger::debug("controls: a menu opened - panning ends");
			g_key = {}, g_pad = {};
			g_zoomDown = Down(s.zoomToggleKey);
			g_panning.store(false);
			g_mousePanning.store(false);
			return;
		}

		Step(g_key, s.hideKey > 0 && Down(s.hideKey), a_shown, "hide key");

		// the controller button: any button, read from the game's own XInput function; a stick click also through the
		// framework when that function is not there. Holding pans with the right stick (the left one when the button is L3).
		const WORD mask = static_cast<WORD>(s.panHoldGamepadButton);
		bool  clicked = false;
		float sx = 0.0f, sy = 0.0f;
		bool  live = false;
		const int  which = mask == XINPUT_GAMEPAD_LEFT_THUMB ? 0 : 1;
		const bool stickOk = s.gamepadHideButton && AMF::GetStick(which, &sx, &sy, &clicked, &live);
		bool       padRead = false;
		const WORD held = s.gamepadHideButton && mask ? PadButtons(&padRead) : 0;
		bool       down = padRead ? (held & mask) != 0 : false;
		if (!padRead && stickOk && (mask == XINPUT_GAMEPAD_LEFT_THUMB || mask == XINPUT_GAMEPAD_RIGHT_THUMB)) down = clicked;
		Step(g_pad, s.gamepadHideButton && mask && down, a_shown, "controller button");

		const bool zoom = s.zoomToggleKey > 0 && Down(s.zoomToggleKey);
		if (zoom && !g_zoomDown) {
			g_toggleZoom.store(true);
			logger::debug("controls: zoom key tapped");
		}
		g_zoomDown = zoom;

		g_panning.store(g_key.panning || g_pad.panning);
		g_mousePanning.store(g_key.panning);
		if (g_pad.panning && stickOk && live) {
			std::scoped_lock l(g_panLock);
			const double px = 400.0 * s.panSpeed * a_dt;   // a full tilt pans 400 px a second
			g_panX += sx * px;
			g_panY -= sy * px;   // stick y is positive up, the map's y down
		}
	}

	bool TakeToggleShown() { return g_toggleShown.exchange(false); }
	bool TakeToggleZoom() { return g_toggleZoom.exchange(false); }
	bool Panning() { return g_panning.load(); }

	std::array<double, 2> TakePan()
	{
		std::scoped_lock l(g_panLock);
		const std::array<double, 2> d{ g_panX, g_panY };
		g_panX = g_panY = 0.0;
		return d;
	}

	bool OnMessage(MSG* a_msg)
	{
		if (!a_msg || a_msg->message != WM_INPUT || !g_mousePanning.load()) {
			return false;
		}
		RAWINPUT in{};
		UINT     size = sizeof(in);
		if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(a_msg->lParam), RID_INPUT, &in, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
			in.header.dwType != RIM_TYPEMOUSE) {
			return false;   // keyboard raw input goes on to the game untouched
		}
		if ((in.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
			std::scoped_lock l(g_panLock);
			const double k = settings::Get().panSpeed;
			g_panX += in.data.mouse.lLastX * k;
			g_panY += in.data.mouse.lLastY * k;
		}
		return true;   // the camera does not also turn while the map pans
	}

	void BeginCapture(int a_target)
	{
		// a key already held when the capture starts is not a new press: the scan starts from the keys down now (the
		// full scan runs only while a key is being bound - never every frame)
		for (UINT vk = 0x08; vk < 0xFF; ++vk) g_vkDown[vk] = (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
		Message("press a key (Escape cancels)", -1);
		g_capture.store(a_target);
		logger::info("controls: capturing the next key for the {} key", a_target == 1 ? "hide" : "zoom");
	}

	void CancelCapture() { g_capture.store(0); }
	int  Capturing() { return g_capture.load(); }

	std::string LastCaptureMessage()
	{
		std::scoped_lock l(g_msgLock);
		return g_message;
	}

	int LastRefusal() { return g_refusal.load(); }
	const char* const* RefusalKeys() { return kRefusalKeys; }
	const char* const* RefusalEnglish() { return kRefusalEnglish; }

	std::string KeyName(std::int32_t a_scan)
	{
		if (a_scan <= 0) return "none";
		const LONG lp = static_cast<LONG>((a_scan & 0x7F) << 16) | (a_scan >= 0x80 ? (1 << 24) : 0);
		wchar_t    buf[64]{};
		const int  n = ::GetKeyNameTextW(lp, buf, 64);
		if (n <= 0) return std::format("key {}", a_scan);
		std::string out;
		for (int i = 0; i < n; ++i) out.push_back(buf[i] < 0x80 ? static_cast<char>(buf[i]) : '?');
		return out;
	}

	void NotePageDrawn() { g_pageDrawnAt.store(NowMs()); }

	std::string PadName(std::int32_t a_mask)
	{
		static constexpr std::pair<WORD, const char*> kNames[] = {
			{ XINPUT_GAMEPAD_A, "A" }, { XINPUT_GAMEPAD_B, "B" }, { XINPUT_GAMEPAD_X, "X" }, { XINPUT_GAMEPAD_Y, "Y" },
			{ XINPUT_GAMEPAD_LEFT_SHOULDER, "LB" }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB" },
			{ XINPUT_GAMEPAD_LEFT_THUMB, "Left stick click (LS)" }, { XINPUT_GAMEPAD_RIGHT_THUMB, "Right stick click (RS)" },
			{ XINPUT_GAMEPAD_DPAD_UP, "D-pad up" }, { XINPUT_GAMEPAD_DPAD_DOWN, "D-pad down" },
			{ XINPUT_GAMEPAD_DPAD_LEFT, "D-pad left" }, { XINPUT_GAMEPAD_DPAD_RIGHT, "D-pad right" },
			{ XINPUT_GAMEPAD_BACK, "Back" }, { XINPUT_GAMEPAD_START, "Start" },
		};
		for (const auto& [bit, name] : kNames) {
			if (static_cast<WORD>(a_mask) == bit) return name;
		}
		return a_mask ? std::format("button 0x{:04X}", a_mask) : "none";
	}

	bool CanBindPad() { return AMF::HasKeyCapture(); }

	void BeginPadCapture()
	{
		AMF::CancelKeyCapture();
		g_padCapture.store(1);
		Message("let go of every button, then press the new one", -1);
		logger::info("controls: capturing the next controller button");
	}

	void CancelPadCapture()
	{
		if (g_padCapture.exchange(0) == 2) AMF::CancelKeyCapture();
		Message("", -1);
	}

	bool PadCapturing() { return g_padCapture.load() != 0; }

	std::int32_t PadCaptureTick()
	{
		const int st = g_padCapture.load();
		if (st == 1) {
			bool ok = false;
			const WORD held = PadButtons(&ok);
			if (!ok || held == 0) {   // everything let go (or no pad to read): the next press is the new button
				AMF::BeginKeyCapture(true, 8000);
				g_padCapture.store(2);
				Message("press a controller button", -1);
			}
			return -1;
		}
		if (st != 2) return -1;
		std::int32_t kind = 0, code = 0;
		switch (AMF::PollKeyCapture(&kind, &code)) {
		case AMF::CaptureState::kCaptured:
			g_padCapture.store(0);
			if (kind == 2 && code > 0 && code <= 0xFFFF) {
				Message("bound to " + PadName(code), -1);
				logger::info("controls: the controller button is now {} (0x{:04X})", PadName(code), code);
				return code;
			}
			Message(kind == 3 || kind == 4 ? "a stick direction or a trigger cannot be the button - press a button" : "that was not a controller button", -1);
			return -1;
		case AMF::CaptureState::kCancelled:
		case AMF::CaptureState::kTimedOut:
			g_padCapture.store(0);
			Message("no button pressed - nothing changed", -1);
			return -1;
		case AMF::CaptureState::kIdle:
			g_padCapture.store(0);   // the framework dropped it (its menu closed)
			return -1;
		default:
			return -1;
		}
	}

	json State()
	{
		const auto& s = settings::Get();
		return { { "hide_key", KeyName(s.hideKey) }, { "zoom_key", KeyName(s.zoomToggleKey) }, { "panning", g_panning.load() },
			{ "mouse_panning", g_mousePanning.load() }, { "hold_hide_to_pan", s.holdHideToPan },
			{ "pad_button", PadName(s.panHoldGamepadButton) }, { "pad_enabled", s.gamepadHideButton }, { "pad_readable", PadReader() != nullptr },
			{ "pad_capturing", g_padCapture.load() },
			{ "capturing", g_capture.load() }, { "capture_message", LastCaptureMessage() } };
	}
}
