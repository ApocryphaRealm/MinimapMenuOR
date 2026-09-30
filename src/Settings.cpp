#include "Settings.h"

#include <charconv>
#include <cstddef>
#include <fstream>
#include <sstream>

namespace settings
{
	namespace
	{
		using K = Field::Kind;
#define MM_ROW(sec, key, kind, member, def, lo, hi, comment) Field{ sec, key, K::kind, offsetof(Values, member), def, lo, hi, comment }
		// an indexed member must land inside its array (std::array's operator[] once sent every row to offset 0-12 - CCM)
		static_assert(offsetof(Values, offsetY[3]) == offsetof(Values, offsetY) + 3 * sizeof(float));

		const std::vector<Field> kTable = {
			MM_ROW("General", "bEnabled", kBool, enabled, 1, 0, 1, "1 = the minimap runs. 0 removes it and puts the game's location banner back where the game puts it."),
			MM_ROW("General", "bShowOnGameStart", kBool, showOnGameStart, 1, 0, 1, "1 = the minimap is shown when a game loads. The hide key only hides it until the next load; this setting is changed on the page."),
			MM_ROW("Display", "uAnchor", kInt, anchor, 1, 0, 3, "Which screen corner the minimap is pinned to: 0 = top left, 1 = top right, 2 = bottom left, 3 = bottom right."),
			MM_ROW("Display", "fOffsetXTopLeft", kFloat, offsetX[0], 0, -2000, 2000, "A nudge per corner, in screen pixels, so each corner keeps its own adjustment. +x is always rightwards and +y always downwards, whichever corner is used; (0, 0) sits flush with the screen edges."),
			MM_ROW("Display", "fOffsetYTopLeft", kFloat, offsetY[0], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetXTopRight", kFloat, offsetX[1], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetYTopRight", kFloat, offsetY[1], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetXBottomLeft", kFloat, offsetX[2], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetYBottomLeft", kFloat, offsetY[2], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetXBottomRight", kFloat, offsetX[3], 0, -2000, 2000, ""),
			MM_ROW("Display", "fOffsetYBottomRight", kFloat, offsetY[3], 0, -2000, 2000, ""),
			MM_ROW("Display", "fScale", kFloat, scale, 0.5, 0.1, 1.5, "The minimap's size. Capped at whatever keeps it within a quarter of the screen."),
			MM_ROW("Display", "uShape", kInt, shape, 0, 0, 1, "0 = square, 1 = circle."),
			MM_ROW("Display", "fOpacity", kFloat, opacity, 1, 0.1, 1, "How opaque the minimap is."),
			MM_ROW("Display", "bLinkLocationPopup", kBool, linkLocationPopup, 1, 0, 1, "1 = the minimap places the game's location banner (the area name that appears when you enter a place): below it at a top corner, above it at a bottom corner. 0 = HUD Position Manager (or the game) places it."),
			MM_ROW("Display", "bFitPopupToMinimap", kBool, fitPopupToMinimap, 1, 0, 1, "1 = the location banner's text is scaled down to fit the minimap's width."),
			MM_ROW("Display", "fPopupScale", kFloat, popupScale, 1, 0.3, 3.0, "The location banner's size, on top of the fit: raise it until it reaches the minimap's edges."),
			MM_ROW("Display", "bPairCompass", kBool, pairCompass, 0, 0, 1, "1 = the compass moves under the minimap (above it at a bottom corner), where the location banner was; the banner goes after the compass."),
			MM_ROW("Display", "bFitCompassToMinimap", kBool, fitCompassToMinimap, 1, 0, 1, "1 = the paired compass is scaled down to the minimap's width."),
			MM_ROW("Display", "fCompassScale", kFloat, compassScale, 1, 0.3, 3.0, "The paired compass's size, on top of the fit: raise it until it reaches the minimap's edges."),
			MM_ROW("Display", "fPopupGap", kFloat, popupGap, 8, 0, 100, "The gap between the minimap and the location banner, in screen pixels."),
			MM_ROW("Map", "iMapImage", kInt, mapImage, 1, 0, 1, "0 = parchment only, 1 = the game's own local map (its wall and path lines) wherever you have opened the Map screen's local map, parchment elsewhere."),
			MM_ROW("Map", "fRadiusMetres", kFloat, radiusMetres, 60, 10, 500, "How far from you the edge of the minimap reaches at the normal zoom, in metres."),
			MM_ROW("Map", "fRadiusInteriorMetres", kFloat, radiusInteriorMetres, 25, 5, 300, "The same reach indoors, in metres - interiors are smaller."),
			MM_ROW("Map", "bFollowCameraRotation", kBool, followCameraRotation, 1, 0, 1, "1 = the map turns with the camera (up is where you look); 0 = north is always up."),
			MM_ROW("Map", "bAlwaysDrawLocalMap", kBool, alwaysDrawLocalMap, 1, 0, 1, "1 = the minimap draws the local map around you at all times, with the game's own capture and wall-line material; 0 = only where you opened the Map screen's local map."),
			MM_ROW("Rendering", "fCaptureWidthMetres", kFloat, captureWidthMetres, 180, 40, 600, "How much of the area around you is drawn, in metres across."),
			MM_ROW("Rendering", "fInteriorCutMetres", kFloat, interiorCutMetres, 2.5, 0.5, 10, "Indoors, how far above your feet the map is cut: ceilings and floors above stay out of it."),
			MM_ROW("Rendering", "fRecaptureMoveFraction", kFloat, recaptureMoveFraction, 0.25, 0.05, 0.45, "The map is drawn again after you move this share of its width (or change cell)."),
			MM_ROW("Rendering", "iMapQuarterTurns", kInt, mapQuarterTurns, 0, 0, 3, "A correction, if the drawn map comes out turned: quarter turns clockwise."),
			MM_ROW("Rendering", "bMapMirror", kBool, mapMirror, 0, 0, 1, "A correction, if the drawn map comes out mirrored."),
			MM_ROW("Rendering", "bSkipWhileWorldSettles", kBool, skipWhileWorldSettles, 1, 0, 1, "1 = the map is not drawn again while the world is loading or for iSettleMs after (a load, going in or out of doors, a teleport) - the last picture stays. Ordinary travel never waits."),
			MM_ROW("Rendering", "iSettleMs", kInt, settleMs, 1500, 0, 10000, "How long a load counts as still settling, in milliseconds."),
			MM_ROW("Rendering", "iRedrawIntervalMs", kInt, redrawIntervalMs, 1000, 0, 10000, "The least time between two drawings of the map, in milliseconds. The frame, markers and arrow keep updating either way."),
			MM_ROW("Controls", "iHideKey", kInt, hideKey, 37, 0, 255, "Keyboard keys are DirectInput scan codes, 0 = none. 37 = K: tap to hide or show the minimap; hold to pan it (below)."),
			MM_ROW("Controls", "iZoomToggleKey", kInt, zoomToggleKey, 38, 0, 255, "38 = L: tap to switch between the two zoom levels."),
			MM_ROW("Controls", "bHoldHideToPan", kBool, holdHideToPan, 1, 0, 1, "1 = holding the hide key pans the map with the mouse (or the right stick); 0 = the hide key only hides and shows, the moment it is pressed."),
			MM_ROW("Controls", "fHoldToPanSecs", kFloat, holdToPanSecs, 0.25, 0.05, 2, "How long the hide key must be held before it pans instead of hiding, in seconds."),
			MM_ROW("Controls", "fZoomDefault", kFloat, zoomDefault, 1, 0.25, 8, "The magnification at the normal zoom level."),
			MM_ROW("Controls", "fZoomZoomedIn", kFloat, zoomZoomedIn, 2, 0.25, 8, "The magnification at the other zoom level."),
			MM_ROW("Controls", "bGamepadHideButton", kBool, gamepadHideButton, 1, 0, 1, "1 = the controller button below also hides (tap) and pans (hold). Gameplay only, never in menus."),
			MM_ROW("Controls", "iPanHoldGamepadButton", kInt, panHoldGamepadButton, 128, 0, 65535, "The controller button, bound on the page (any button). An XInput mask: 128 = right stick click (the default), 64 = left stick click, 4096 = A, 8 = D-pad right."),
			MM_ROW("Controls", "fPanSpeed", kFloat, panSpeed, 1, 0.1, 5, "How fast the map pans."),
			MM_ROW("Markers", "bDoors", kBool, markDoors, 1, 0, 1, "1 = doors on the minimap."),
			MM_ROW("Markers", "bLocations", kBool, markLocations, 1, 0, 1, "1 = locations (cities, caves, forts, shrines...)."),
			MM_ROW("Markers", "bQuestTargets", kBool, markQuestTargets, 1, 0, 1, "1 = quest targets; one beyond the edge stays on the rim, pointing the way."),
			MM_ROW("Markers", "bFarLocationsOnRim", kBool, farLocationsOnRim, 1, 0, 1, "1 = a location beyond the edge stays on the rim, dimmed, pointing the way (as the compass shows it)."),
			MM_ROW("Markers", "bHostiles", kBool, markHostiles, 0, 0, 1, "1 = enemies the compass shows, as red dots."),
			MM_ROW("Markers", "fIconScale", kFloat, iconScale, 1, 0.3, 3, "The size of the icons."),
			MM_ROW("Log", "uLogLevel", kInt, logLevel, 2, 0, 6, "0 trace, 1 debug, 2 info, 3 warnings, 4 errors, 5 critical, 6 off. Use 1 when reporting a problem."),
		};
#undef MM_ROW

		Values     g_values;
		std::mutex g_saveLock;
		bool       g_tableBroken = false;   // two rows share memory: never load or save (the INI is left as it is)

		std::size_t SizeOf(const Field& a_f) { return a_f.kind == K::kBool ? sizeof(bool) : 4; }

		// Every row must own its bytes inside Values, and no two rows may share any: a row that points at the wrong
		// member reads and writes that member (2026-09-29 - the framing rows landed on enabled / cameraStyle).
		bool TableSound()
		{
			static const bool sound = [] {
				bool ok = true;
				for (std::size_t i = 0; i < kTable.size(); ++i) {
					const auto& a = kTable[i];
					if (a.offset + SizeOf(a) > sizeof(Values)) {
						logger::critical("settings: [{}] {} points outside the settings block - a build defect", a.section, a.key);
						ok = false;
					}
					for (std::size_t j = i + 1; j < kTable.size(); ++j) {
						const auto& b = kTable[j];
						if (a.offset < b.offset + SizeOf(b) && b.offset < a.offset + SizeOf(a)) {
							logger::critical("settings: [{}] {} and [{}] {} share memory (offset {} / {}) - a build defect", a.section, a.key, b.section, b.key, a.offset, b.offset);
							ok = false;
						}
					}
				}
				if (!ok) logger::critical("settings: the table is unsound - Minimap Menu runs on its compiled defaults and will not read or write the INI");
				return ok;
			}();
			return sound;
		}

		std::string_view Trim(std::string_view a_s)
		{
			while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t')) a_s.remove_prefix(1);
			while (!a_s.empty() && (a_s.back() == ' ' || a_s.back() == '\t' || a_s.back() == '\r')) a_s.remove_suffix(1);
			return a_s;
		}

		std::string Lower(std::string a_s)
		{
			std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::uint8_t* Slot(Values& a_v, const Field& a_f) { return reinterpret_cast<std::uint8_t*>(&a_v) + a_f.offset; }
		const std::uint8_t* Slot(const Values& a_v, const Field& a_f) { return reinterpret_cast<const std::uint8_t*>(&a_v) + a_f.offset; }

		double Read(const Values& a_v, const Field& a_f)
		{
			switch (a_f.kind) {
			case K::kBool:  return *reinterpret_cast<const bool*>(Slot(a_v, a_f)) ? 1.0 : 0.0;
			case K::kInt:   return *reinterpret_cast<const std::int32_t*>(Slot(a_v, a_f));
			default:        return *reinterpret_cast<const float*>(Slot(a_v, a_f));
			}
		}

		void Write(Values& a_v, const Field& a_f, double a_x)
		{
			a_x = std::clamp(a_x, a_f.min, a_f.max);
			switch (a_f.kind) {
			case K::kBool:  *reinterpret_cast<bool*>(Slot(a_v, a_f)) = a_x != 0.0; break;
			case K::kInt:   *reinterpret_cast<std::int32_t*>(Slot(a_v, a_f)) = static_cast<std::int32_t>(a_x); break;
			default:        *reinterpret_cast<float*>(Slot(a_v, a_f)) = static_cast<float>(a_x); break;
			}
		}

		std::string Format(const Field& a_f, double a_x)
		{
			switch (a_f.kind) {
			case K::kBool:  return a_x != 0.0 ? "1" : "0";
			case K::kInt:   return std::to_string(static_cast<std::int32_t>(a_x));
			default:        return std::format("{:.2f}", a_x);
			}
		}

		std::optional<double> Parse(const Field& a_f, std::string_view a_text)
		{
			a_text = Trim(a_text);
			if (a_text.empty()) {
				return std::nullopt;
			}
			if (a_f.kind == K::kFloat) {
				double x = 0.0;
				const auto r = std::from_chars(a_text.data(), a_text.data() + a_text.size(), x);
				return r.ec == std::errc{} ? std::optional<double>(x) : std::nullopt;
			}
			std::int64_t x = 0;
			const bool hex = a_text.size() > 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X');
			const auto* b = a_text.data() + (hex ? 2 : 0);
			const auto r = std::from_chars(b, a_text.data() + a_text.size(), x, hex ? 16 : 10);
			return r.ec == std::errc{} ? std::optional<double>(static_cast<double>(x)) : std::nullopt;
		}

		std::filesystem::path ModuleFolder()
		{
			HMODULE self = nullptr;
			::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ModuleFolder), &self);
			wchar_t buf[MAX_PATH]{};
			const DWORD n = self ? ::GetModuleFileNameW(self, buf, MAX_PATH) : 0;
			if (n == 0 || n >= MAX_PATH) {
				return std::filesystem::path(L"OBSE") / L"Plugins";   // relative to the game's working folder
			}
			return std::filesystem::path(buf).parent_path();
		}
	}

	const std::vector<Field>& Table() { return kTable; }
	Values& Get() { return g_values; }
	Values Defaults() { return Values{}; }
	std::filesystem::path PluginFolder() { return ModuleFolder(); }
	std::filesystem::path IniPath() { return ModuleFolder() / L"MinimapMenu.ini"; }

	void Load()
	{
		if (!TableSound()) {
			g_tableBroken = true;
			return;
		}
		// Rule 16, checked where it can be: the member initialisers and the table must agree.
		const Values d{};
		for (const auto& f : kTable) {
			if (std::abs(Read(d, f) - f.def) > 1e-6) {
				logger::error("settings: compiled default of [{}] {} is {} but the table says {} - a build defect, report it",
					f.section, f.key, Read(d, f), f.def);
			}
		}

		std::ifstream file(IniPath());
		if (!file.is_open()) {
			logger::info("settings: {} not found - the compiled defaults are in effect (they match the shipped INI)", IniPath().string());
			return;
		}
		std::unordered_map<std::string, std::string> entries;
		std::string line, section;
		while (std::getline(file, line)) {
			const auto t = Trim(line);
			if (t.empty() || t.front() == ';' || t.front() == '#') continue;
			if (t.front() == '[' && t.back() == ']') {
				section = Lower(std::string(Trim(t.substr(1, t.size() - 2))));
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) continue;
			entries[section + "." + Lower(std::string(Trim(t.substr(0, eq))))] = std::string(Trim(t.substr(eq + 1)));
		}
		for (const auto& f : kTable) {
			const auto it = entries.find(Lower(std::string(f.section)) + "." + Lower(std::string(f.key)));
			if (it == entries.end()) {
				logger::debug("settings: [{}] {} not in the INI - compiled default {}", f.section, f.key, Format(f, f.def));
				continue;
			}
			const auto x = Parse(f, it->second);
			if (!x) {
				logger::warn("settings: [{}] {} = \"{}\" is not a number - keeping {}", f.section, f.key, it->second, Format(f, Read(g_values, f)));
				continue;
			}
			if (*x < f.min || *x > f.max) {
				logger::warn("settings: [{}] {} = {} is outside {}..{} - clamped", f.section, f.key, *x, f.min, f.max);
			}
			Write(g_values, f, *x);
		}
		logger::info("settings loaded from {}: enabled={}, corner {}, scale {:.2f}, shape {}, keys hide={:#x} zoom={:#x}, pad {:#x}, log level {}",
			IniPath().string(), g_values.enabled, g_values.anchor, g_values.scale, g_values.shape, g_values.hideKey, g_values.zoomToggleKey,
			g_values.panHoldGamepadButton, g_values.logLevel);
	}

	bool Save()
	{
		if (g_tableBroken || !TableSound()) return false;   // never write through a table that points at the wrong members
		std::scoped_lock l(g_saveLock);
		const auto path = IniPath();
		std::vector<std::string> lines;
		bool crlf = true;
		{
			std::ifstream in(path, std::ios::binary);
			if (in.is_open()) {
				std::stringstream ss;
				ss << in.rdbuf();
				const std::string all = ss.str();
				crlf = all.find("\r\n") != std::string::npos || all.empty();
				std::string cur;
				for (char c : all) {
					if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); lines.push_back(cur); cur.clear(); }
					else cur.push_back(c);
				}
				if (!cur.empty()) lines.push_back(cur);
			}
		}
		if (lines.empty()) {
			std::istringstream def(DefaultIniText());
			for (std::string s; std::getline(def, s);) { if (!s.empty() && s.back() == '\r') s.pop_back(); lines.push_back(s); }
		}

		std::vector<bool> written(kTable.size(), false);
		std::string section;
		for (auto& ln : lines) {
			const auto t = Trim(ln);
			if (!t.empty() && t.front() == '[' && t.back() == ']') { section = Lower(std::string(Trim(t.substr(1, t.size() - 2)))); continue; }
			if (t.empty() || t.front() == ';' || t.front() == '#') continue;
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) continue;
			const std::string key = Lower(std::string(Trim(t.substr(0, eq))));
			for (std::size_t i = 0; i < kTable.size(); ++i) {
				if (Lower(kTable[i].section) == section && Lower(kTable[i].key) == key) {
					ln = std::string(kTable[i].key) + "=" + Format(kTable[i], Read(g_values, kTable[i]));
					written[i] = true;
					break;
				}
			}
		}
		// keys the file did not have yet go at the end of their section (a new section at the end if needed)
		for (std::size_t i = 0; i < kTable.size(); ++i) {
			if (written[i]) continue;
			const std::string want = Lower(kTable[i].section);
			std::size_t insertAt = lines.size();
			bool found = false;
			std::string sec;
			for (std::size_t j = 0; j < lines.size(); ++j) {
				const auto t = Trim(lines[j]);
				if (!t.empty() && t.front() == '[' && t.back() == ']') {
					if (found) { insertAt = j; break; }
					sec = Lower(std::string(Trim(t.substr(1, t.size() - 2))));
					found = sec == want;
				}
			}
			if (!found) {
				lines.push_back("");
				lines.push_back(std::string("[") + kTable[i].section + "]");
				insertAt = lines.size();
			} else {
				// before the blank lines that separate this section from the next one
				while (insertAt > 0 && Trim(lines[insertAt - 1]).empty()) --insertAt;
			}
			lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAt), std::string(kTable[i].key) + "=" + Format(kTable[i], Read(g_values, kTable[i])));
		}

		const auto tmp = std::filesystem::path(path).concat(L".tmp");
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				logger::error("settings: could not write {} - this change lasts only until the game closes", path.string());
				return false;
			}
			for (const auto& ln : lines) { out << ln << (crlf ? "\r\n" : "\n"); }
		}
		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec) {
			logger::error("settings: could not replace {} ({}) - this change lasts only until the game closes", path.string(), ec.message());
			return false;
		}
		logger::debug("settings: saved {}", path.string());
		return true;
	}

	bool SetByName(const std::string& a_name, const json& a_value, std::string& a_why)
	{
		if (g_tableBroken || !TableSound()) {
			a_why = "the settings table is unsound (see the log) - nothing is written";
			return false;
		}
		const auto dot = a_name.rfind('.');   // sections may hold a dot (Framing.Sneaking), keys never do
		const std::string sec = Lower(dot == std::string::npos ? "" : a_name.substr(0, dot));
		const std::string key = Lower(dot == std::string::npos ? a_name : a_name.substr(dot + 1));
		for (const auto& f : kTable) {
			if ((sec.empty() || Lower(f.section) == sec) && Lower(f.key) == key) {
				double x = 0.0;
				if (a_value.is_boolean()) x = a_value.get<bool>() ? 1.0 : 0.0;
				else if (a_value.is_number()) x = a_value.get<double>();
				else { a_why = "value must be a number or a boolean"; return false; }
				if (x < f.min || x > f.max) { a_why = std::format("{} is outside {}..{}", x, f.min, f.max); return false; }
				Write(g_values, f, x);
				logger::info("settings: [{}] {} = {} (set by the driving tool)", f.section, f.key, Format(f, x));
				return Save() || (a_why = "set, but the INI could not be written", false);
			}
		}
		a_why = "no setting named " + a_name;
		return false;
	}

	json GetAll()
	{
		json j = json::object();
		for (const auto& f : kTable) {
			const double x = Read(g_values, f);
			j[std::string(f.section) + "." + f.key] = f.kind == K::kBool ? json(x != 0.0) : f.kind == K::kInt ? json(static_cast<std::int32_t>(x)) : json(x);
		}
		return j;
	}

	std::string DefaultIniText()
	{
		std::string s = "; Minimap Menu (Oblivion Remastered). Every setting is also on the Minimap Menu page of the\n"
		                "; Apocrypha Menu Framework, which rewrites this file; edits made while the game is closed are read at start.\n";
		std::string section;
		for (const auto& f : kTable) {
			if (section != f.section) {
				section = f.section;
				s += "\n[" + section + "]\n";
			}
			if (f.comment && *f.comment) s += std::string("; ") + f.comment + "\n";
			s += std::string(f.key) + "=" + Format(f, f.def) + "\n";
		}
		return s;
	}
}
