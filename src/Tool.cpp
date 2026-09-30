#include "Tool.h"

#include "AMF.h"
#include "Controls.h"
#include "Minimap.h"
#include "Page.h"
#include "Popup.h"
#include "Settings.h"
#include "TestBenchAPI.h"

namespace tool
{
	namespace
	{
		TestBenchAPI::ITestBenchInterface001* g_tb = nullptr;

		void Write(void* a_sink, TestBenchAPI::WriteFn a_write, const json& a_j) { a_write(a_sink, a_j.dump().c_str()); }

		void StatusTool(void*, const char*, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			Write(a_sink, a_write, { { "ok", true }, { "version", MM_VERSION }, { "minimap", minimap::State() }, { "popup", popup::State() },
				{ "controls", controls::State() }, { "owns_location_popup", minimap::OwnsLocationPopup() }, { "owns_compass", minimap::OwnsCompass() }, { "settings", settings::GetAll() } });
		}

		void DriveTool(void*, const char* a_args, void* a_sink, TestBenchAPI::WriteFn a_write)
		{
			json args = json::parse(a_args ? a_args : "{}", nullptr, false);
			if (args.is_discarded()) {
				Write(a_sink, a_write, { { "ok", false }, { "error", "args are not JSON" } });
				return;
			}
			const std::string op = args.value("op", "");
			if (op == "get") {
				Write(a_sink, a_write, { { "ok", true }, { "settings", settings::GetAll() } });
			} else if (op == "set") {
				std::string why;
				const bool  ok = args.contains("key") && args.contains("value") && settings::SetByName(args["key"].get<std::string>(), args["value"], why);
				Write(a_sink, a_write, { { "ok", ok }, { "error", ok ? "" : (why.empty() ? "needs key and value" : why) } });
			} else if (op == "action") {
				const std::string n = args.value("name", "");
				const std::unordered_map<std::string, minimap::Action> m = { { "toggle", minimap::Action::kToggleShown },
					{ "zoom", minimap::Action::kToggleZoom }, { "recentre", minimap::Action::kRecentre }, { "rebuild", minimap::Action::kRebuild } };
				const auto it = m.find(n);
				if (it == m.end()) {
					Write(a_sink, a_write, { { "ok", false }, { "error", "name: toggle|zoom|recentre|rebuild" } });
					return;
				}
				minimap::Queue(it->second);
				Write(a_sink, a_write, { { "ok", true }, { "queued", n }, { "note", "applied on the next game tick; read minimap.status" } });
			} else if (op == "bind") {
				const std::string t = args.value("target", "");
				if (t != "hide" && t != "zoom") {
					Write(a_sink, a_write, { { "ok", false }, { "error", "target: hide|zoom (the next key pressed is captured)" } });
					return;
				}
				controls::BeginCapture(t == "hide" ? 1 : 2);
				Write(a_sink, a_write, { { "ok", true }, { "capturing", t } });
			} else if (op == "page") {
				Write(a_sink, a_write, { { "ok", AMF::OpenMenu(page::kModName) } });
			} else {
				Write(a_sink, a_write, { { "ok", false }, { "error", "op: get | set{key,value} | action{name} | bind{target} | page" } });
			}
		}
	}

	bool Register()
	{
		if (g_tb) return true;
		HMODULE tb = ::GetModuleHandleW(L"TestBench.dll");
		auto    get = tb ? reinterpret_cast<void* (*)(unsigned)>(::GetProcAddress(tb, "TestBench_GetInterface")) : nullptr;
		g_tb = get ? static_cast<TestBenchAPI::ITestBenchInterface001*>(get(1)) : nullptr;
		if (!g_tb) return false;
		g_tb->RegisterTool("minimap.status",
			R"({"description":"Minimap Menu: the live state (widget, layout rectangle, viewport, zoom, pan, heading, the compass markers read and drawn with a raw sample, the location banner link, the controls, settings).","inputSchema":{"type":"object","properties":{}},"readOnly":true})",
			&StatusTool, nullptr);
		g_tb->RegisterTool("minimap.drive",
			R"({"description":"Minimap Menu - drive it for testing. op: get | set {key:'Section.Key', value} (same path as the page, saved to the INI) | action {name: toggle|zoom|recentre|rebuild} | bind {target: hide|zoom} (captures the next key pressed) | page (opens AMF on Minimap Menu).","inputSchema":{"type":"object","properties":{"op":{"type":"string"},"key":{"type":"string"},"value":{},"name":{"type":"string"},"target":{"type":"string"}}},"readOnly":false})",
			&DriveTool, nullptr);
		logger::info("TestBench tools registered: minimap.status, minimap.drive");
		return true;
	}
}
