#include "LocalMap.h"

#include "Ue.h"

namespace localmap
{
	namespace
	{
		constexpr const wchar_t* kMapWidgetClass = L"/Script/Altar.VModern_NavigableMapWidget";
		constexpr const wchar_t* kPageViewModelClass = L"/Script/Altar.VMapPageViewModel";
		constexpr const wchar_t* kManagerClass = L"/Script/Altar.LocalMapManager";

		ue::Handle g_material;          // the game's own MID, kept referenced by our image's brush once shown
		ue::Handle g_manager;
		double     g_mapW = 0, g_mapH = 0;   // the map's size in map units (FLegacyMapMenuLocalMapProperties.MapSize)
		RE::TESObjectCELL* g_cell = nullptr;  // the cell the player was in when the map was read
		ULONGLONG  g_nextScan = 0, g_nextManager = 0;
		int        g_reads = 0;

		std::mutex  g_lock;
		std::string g_status = "the Map screen's local map has not been opened yet";
		json        g_last = json::object();

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) logger::info("localmap: {}", a_s);
			g_status = std::move(a_s);
		}

		UE::UObject* ObjectProp(UE::UObject* a_o, const char* a_name)
		{
			auto* cls = a_o ? a_o->GetClass() : nullptr;
			const auto off = cls ? ue::Offset(cls, a_name) : -1;
			auto** p = off >= 0 ? ue::At<UE::UObject*>(a_o, off) : nullptr;
			return p && *p && ue::IsLive(*p) ? *p : nullptr;
		}

		// the map's size from the local page's MapImage brush (probe 5: 4096 x 4096 - the page view model's MapSize reads 0)
		bool ReadImageSize(UE::UObject* a_widget)
		{
			static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
			auto* image = ObjectProp(a_widget, "MapImage");
			auto* cls = image ? image->GetClass() : nullptr;
			const auto brushOff = cls ? ue::Offset(cls, "Brush") : -1;
			const auto sizeOff = brushStruct ? ue::Offset(brushStruct, "ImageSize") : -1;
			const auto sizeBytes = brushStruct ? ue::SizeOf(brushStruct, "ImageSize") : -1;
			if (brushOff < 0 || sizeOff < 0) return false;
			const auto* at = reinterpret_cast<const std::uint8_t*>(image) + brushOff + sizeOff;
			double w = 0, h = 0;
			if (sizeBytes == 16) {
				double v[2]{};
				std::memcpy(v, at, sizeof(v));
				w = v[0], h = v[1];
			} else if (sizeBytes == 8) {
				float v[2]{};
				std::memcpy(v, at, sizeof(v));
				w = v[0], h = v[1];
			}
			if (w > 1 && h > 1) {
				g_mapW = w;
				g_mapH = h;
				return true;
			}
			return false;
		}

		// the map page's size, from the page view model that holds the local map's properties (a fallback: it read 0 in game)
		void ReadMapSize()
		{
			static auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyMapMenuLocalMapProperties"));
			auto* cls = ue::Class(kPageViewModelClass);
			if (!cls || !props) return;
			const auto propsOff = ue::Offset(cls, "LocalMapProperties");
			const auto mapOff = ue::Offset(props, "Map");
			const auto sizeOff = ue::Offset(props, "MapSize");
			if (propsOff < 0 || mapOff < 0 || sizeOff < 0) return;
			for (auto* vm : ue::AllOf(cls)) {
				auto* base = reinterpret_cast<std::uint8_t*>(vm) + propsOff;
				auto* map = *reinterpret_cast<UE::UObject**>(base + mapOff);
				if (!map) continue;
				double size[2]{};
				std::memcpy(size, base + sizeOff, sizeof(size));
				if (size[0] > 1 && size[1] > 1) {
					g_mapW = size[0];
					g_mapH = size[1];
					return;
				}
			}
		}

		UE::UObject* Manager()
		{
			if (auto* m = g_manager.Get()) return m;
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextManager) return nullptr;
			g_nextManager = now + 2000;
			auto* cls = ue::Class(kManagerClass);
			auto* m = cls ? ue::FirstOf(cls) : nullptr;
			g_manager.Set(m);
			if (m) logger::info("localmap: the game's local-map manager is {}", ue::NameOf(m));
			return m;
		}

		bool Coordinates(UE::UObject* a_mgr, const std::array<double, 3>& a_world, std::array<double, 2>& a_out)
		{
			ue::Call c(a_mgr, L"GetLocalMapCoordinates");
			if (!c || !c.Set("WorldPosition", a_world) || !c.Run()) return false;
			a_out = c.Get<std::array<double, 2>>("ReturnValue");
			return std::isfinite(a_out[0]) && std::isfinite(a_out[1]);
		}

		bool PlayerWorld(std::array<double, 3>& a_out)
		{
			auto* pc = ue::PlayerController();
			static ue::Getter getPawn(L"K2_GetPawn");
			static ue::Getter location(L"K2_GetActorLocation");
			UE::UObject* pawn = nullptr;
			return pc && getPawn.Get(pc, pawn) && pawn && ue::IsLive(pawn) && location.Get(pawn, a_out);
		}
	}

	void Tick(bool a_menuOpen)
	{
		if (!a_menuOpen) return;
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextScan) return;
		g_nextScan = now + 500;   // an object-array scan while a menu is open: twice a second at most
		auto* cls = ue::Class(kMapWidgetClass);
		if (!cls) return;
		for (auto* w : ue::AllOf(cls)) {
			auto* mid = ObjectProp(w, "LocalMapMaterialDynamic");
			if (!mid) continue;
			if (!ReadImageSize(w)) ReadMapSize();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (mid != g_material.Get() || (player && player->parentCell != g_cell)) {
				g_material.Set(mid);
				g_cell = player ? player->parentCell : nullptr;
				++g_reads;
				Status(std::format("read the game's local map ({}; map size {:.0f} x {:.0f}) - shown on the minimap while you are on it", ue::NameOf(mid), g_mapW, g_mapH));
			}
			return;
		}
	}

	Placement Place(double a_centreX, double a_centreY, double a_pxPerCm, double a_upDeg)
	{
		Placement p;
		auto* mid = g_material.Get();
		auto* mgr = Manager();
		std::array<double, 3> w{};
		if (!mid || !mgr || !PlayerWorld(w)) return p;
		std::array<double, 2> at{}, east{}, north{};
		// Unreal's world axes: +X east, -Y north (Oblivion's +y) - one metre each way, so the map's own scale and
		// rotation come from the game's helper rather than from an assumption
		if (!Coordinates(mgr, w, at) || !Coordinates(mgr, { w[0] + 100.0, w[1], w[2] }, east) || !Coordinates(mgr, { w[0], w[1] - 100.0, w[2] }, north)) {
			return p;
		}
		const double ex = east[0] - at[0], ey = east[1] - at[1];
		const double nx = north[0] - at[0], ny = north[1] - at[1];
		const double unitsPerCm = std::hypot(ex, ey) / 100.0;
		const double mapW = g_mapW > 1 ? g_mapW : 4096.0, mapH = g_mapH > 1 ? g_mapH : 4096.0;   // 4096: the local page's image (probe 5)
		if (unitsPerCm <= 1e-9) return p;
		// normalised coordinates (0..1 across the map) or map units: a metre is far less than 1% of a map in the former
		const bool normalised = std::hypot(ex, ey) < 0.05;
		const double ux = normalised ? at[0] * mapW : at[0];
		const double uy = normalised ? at[1] * mapH : at[1];
		const double perCm = normalised ? unitsPerCm * mapW : unitsPerCm;   // map units per cm
		const bool   onMap = ux >= 0 && uy >= 0 && ux <= mapW && uy <= mapH;
		const double s = a_pxPerCm / perCm;   // panel units per map unit
		// the image's north (up the map by the helper) -> the compass bearing that must point up on the minimap
		const double northAngle = std::atan2(nx, -ny) * 180.0 / std::numbers::pi;   // clockwise from up, in the image
		p.material = mid;
		p.width = mapW * s;
		p.height = mapH * s;
		p.x = a_centreX - ux * s;
		p.y = a_centreY - uy * s;
		p.pivotX = ux / mapW;
		p.pivotY = uy / mapH;
		p.angle = -a_upDeg - northAngle;
		p.ok = onMap;
		std::scoped_lock l(g_lock);
		g_last = { { "player_world", w }, { "map_at", at }, { "east_step", { ex, ey } }, { "north_step", { nx, ny } },
			{ "normalised", normalised }, { "map_units_per_cm", perCm }, { "map_size", { mapW, mapH } }, { "on_map", onMap },
			{ "image", { p.x, p.y, p.width, p.height } }, { "angle", p.angle } };
		return p;
	}

	json State()
	{
		std::scoped_lock l(g_lock);
		json j = g_last;
		j["status"] = g_status;
		j["reads"] = g_reads;
		return j;
	}
}
