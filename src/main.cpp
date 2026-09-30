// Minimap Menu (Oblivion Remastered, OBSE64) - entry point.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Controls.h"
#include "Minimap.h"
#include "Page.h"
#include "Settings.h"
#include "Tick.h"
#include "Tool.h"

namespace
{
	// every frame, on the game thread (the PeekMessageW tick - it runs while a menu has the game paused too)
	void OnFrame()
	{
		static bool          toolRegistered = false;
		static std::uint64_t n = 0;
		if (!toolRegistered && ++n % 60 == 0) {
			toolRegistered = tool::Register();   // TestBench is a test-only plugin: asked again until it answers
		}
		minimap::Tick();
	}

	void OnMessage(OBSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) return;
		if (a_msg->type == OBSE::MessagingInterface::kPostLoad) {
			page::Register();
			if (!tool::Register()) logger::debug("TestBench not loaded yet - the driving tools are tried again shortly");
			tick::Install(&OnFrame);
			tick::SetMessageFilter(&controls::OnMessage);   // raw mouse movement while the hide key pans the map
		}
	}

	// the previous launch's log, kept as MinimapMenu.prev.log before OBSE::Init truncates it
	void KeepPreviousLog()
	{
		PWSTR docs = nullptr;
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) || !docs) {
			return;
		}
		const std::filesystem::path dir = std::filesystem::path(docs) / L"My Games" / L"Oblivion Remastered" / L"OBSE" / L"Logs";
		::CoTaskMemFree(docs);
		std::error_code ec;
		if (std::filesystem::exists(dir / L"MinimapMenu.log", ec)) {
			std::filesystem::copy_file(dir / L"MinimapMenu.log", dir / L"MinimapMenu.prev.log", std::filesystem::copy_options::overwrite_existing, ec);
		}
	}
}

// HUD Position Manager OR asks this before moving the location banner (its "Location" element): true while Minimap
// Menu's link is on, so the two never write the same render translation (agreed with the agent that owns HPM OR).
extern "C" __declspec(dllexport) bool MinimapMenu_OwnsLocationPopup()
{
	return minimap::OwnsLocationPopup();
}

OBSE_PLUGIN_LOAD(const OBSE::LoadInterface* a_obse)
{
	KeepPreviousLog();
	OBSE::Init(a_obse);
	settings::Load();
	{
		const auto level = static_cast<spdlog::level::level_enum>(std::clamp(settings::Get().logLevel, 0, 6));
		logger::set_level(level, level);
	}
	logger::info("Minimap Menu {} (Oblivion Remastered). Log level {} - set [Log] uLogLevel=1 in MinimapMenu.ini for more detail "
				 "when reporting a problem.", MM_VERSION, settings::Get().logLevel);

	if (auto* messaging = OBSE::GetMessagingInterface()) {
		if (!messaging->RegisterListener(&OnMessage)) {
			logger::error("OBSE messaging refused the listener - Minimap Menu will not start");
		}
	} else {
		logger::error("OBSE messaging interface missing - Minimap Menu will not start");
	}
	return true;
}
