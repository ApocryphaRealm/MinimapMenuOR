#include "Popup.h"

#include "Ue.h"

namespace popup
{
	namespace
	{
		constexpr const wchar_t* kAreaClass = L"/Game/UI/Modern/HUD/WBP_ModernHud_Area.WBP_ModernHud_Area_C";

		ue::Handle g_area;
		ULONGLONG  g_nextFind = 0, g_nextPass = 0;
		bool       g_placing = false;
		int        g_candidates = 0;
		double     g_tx = 0.0, g_ty = 0.0;   // our render translation, in the widget's own units
		bool       g_translated = false;
		int        g_geometryMode = 0;       // 0 unknown, 1 the cached geometry includes the translation, 2 it does not
		double     g_lastX = 0.0, g_lastY = 0.0;

		std::mutex  g_lock;
		std::string g_status = "not found yet";
		json        g_last = json::object();

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) logger::info("popup: {}", a_s);
			g_status = std::move(a_s);
		}

		bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_w2, double& a_h, double& a_unitsPerLocal);

		// the banner the HUD actually lays out: live instances only (never the class's template, which is never laid
		// out), the first with a measurable size. A hidden banner has no size yet: asked again every second (rule 17).
		UE::UObject* Area()
		{
			if (auto* a = g_area.Get()) return a;
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextFind) return nullptr;
			g_nextFind = now + 1000;
			auto* cls = ue::Class(kAreaClass);
			const auto all = cls ? ue::AllOf(cls) : std::vector<UE::UObject*>{};
			g_candidates = static_cast<int>(all.size());
			UE::UObject* found = nullptr;
			for (auto* w : all) {
				double x = 0, y = 0, bw = 0, bh = 0, k = 0;
				if (Measure(w, x, y, bw, bh, k)) {
					found = w;
					break;
				}
			}
			g_area.Set(found);
			g_translated = false;
			g_geometryMode = 0;
			if (found) Status(std::format("found the laid-out location banner ({} of {} live)", ue::NameOf(found), all.size()));
			else Status(!cls ? "the location banner's class is not loaded yet"
			                 : (all.empty() ? "no live location banner yet" : std::format("{} live banner(s), none laid out yet (it shows when you enter an area)", all.size())));
			return found;
		}

		// the widget's own current render translation (whoever wrote it - a leftover HUD Position Manager offset included)
		std::array<double, 2> CurrentTranslation(UE::UObject* a_w)
		{
			static auto* xform = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/UMG.WidgetTransform"));
			std::array<double, 2> t{ g_tx, g_ty };
			auto* cls = a_w ? a_w->GetClass() : nullptr;
			const auto off = cls ? ue::Offset(cls, "RenderTransform") : -1;
			const auto tr = xform ? ue::Offset(xform, "Translation") : -1;
			if (off >= 0 && tr >= 0) std::memcpy(t.data(), reinterpret_cast<std::uint8_t*>(a_w) + off + tr, sizeof(t));
			return t;
		}

		void Translate(UE::UObject* a_w, double a_x, double a_y)
		{
			const double t[2] = { a_x, a_y };
			ue::CallFirst(a_w, L"SetRenderTranslation", t, sizeof(t));
			g_tx = a_x;
			g_ty = a_y;
			g_translated = a_x != 0.0 || a_y != 0.0;
		}

		// where the widget sits on the viewport (viewport units) and how big, from its cached geometry
		bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_w2, double& a_h, double& a_unitsPerLocal)
		{
			auto* pc = ue::PlayerController();
			static auto* lib = ue::Class(L"/Script/UMG.SlateBlueprintLibrary");
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!pc || !cdo) return false;
			ue::Call geo(a_w, L"GetCachedGeometry");
			const auto gsize = geo.Size("ReturnValue");
			if (!geo || gsize <= 0 || !geo.Run()) return false;
			const auto toViewport = [&](double a_lx, double a_ly, double& a_vx, double& a_vy) {
				ue::Call c(cdo, L"LocalToViewport");
				void* g = c.At("Geometry");
				if (!c || !g || c.Size("Geometry") != gsize) return false;
				c.Set("WorldContextObject", pc);
				std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
				const double local[2] = { a_lx, a_ly };
				c.Set("LocalCoordinate", local);
				c.Run();
				const auto vp = c.Get<std::array<double, 2>>("ViewportPosition");
				a_vx = vp[0];
				a_vy = vp[1];
				return true;
			};
			ue::Call size(cdo, L"GetLocalSize");
			void* g = size.At("Geometry");
			if (!size || !g || size.Size("Geometry") != gsize) return false;
			std::memcpy(g, geo.At("ReturnValue"), static_cast<std::size_t>(gsize));
			size.Run();
			const auto local = size.Get<std::array<double, 2>>("ReturnValue");
			double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (!toViewport(0.0, 0.0, x0, y0) || !toViewport(1.0, 0.0, x1, y1)) return false;
			a_unitsPerLocal = x1 - x0;
			a_x = x0;
			a_y = y0;
			a_w2 = local[0] * a_unitsPerLocal;
			a_h = local[1] * a_unitsPerLocal;
			return a_unitsPerLocal > 0.0001 && local[0] > 0.0 && local[1] > 0.0;
		}
	}

	void Tick(const Rect& a_map, bool a_active, double a_gapUnits)
	{
		auto* w = Area();
		if (!w) {
			g_placing = false;
			return;
		}
		if (!a_active || !a_map.valid) {
			if (g_translated) {
				Translate(w, 0.0, 0.0);
				Status("the game's own place is back");
			}
			g_placing = false;
			return;
		}
		g_placing = true;
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextPass) return;
		g_nextPass = now + 100;
		double x = 0, y = 0, bw = 0, bh = 0, k = 1;
		if (!Measure(w, x, y, bw, bh, k)) {
			return;   // not laid out right now: asked again next pass
		}
		// learn once whether the cached geometry already includes the translation (it moved by it after our write)
		if (g_geometryMode == 0 && g_translated) {
			const double moved = std::hypot(x - g_lastX, y - g_lastY);
			g_geometryMode = moved > 1.0 ? 1 : 2;
			logger::info("popup: the cached geometry {} the render translation", g_geometryMode == 1 ? "includes" : "does not include");
		}
		// the banner's own place without any translation (ours, or one another mod left on it)
		const auto   cur = CurrentTranslation(w);
		const double baseX = g_geometryMode == 2 ? x : x - cur[0] * k;
		const double baseY = g_geometryMode == 2 ? y : y - cur[1] * k;
		const double wantX = a_map.x + a_map.w * 0.5 - bw * 0.5;
		const double wantY = a_map.anchoredTop ? a_map.y + a_map.h + a_gapUnits : a_map.y - a_gapUnits - bh;
		const double tx = (wantX - baseX) / k;
		const double ty = (wantY - baseY) / k;
		if (std::abs(tx - g_tx) > 0.5 || std::abs(ty - g_ty) > 0.5) {
			Translate(w, tx, ty);
			Status(std::format("following the minimap ({})", a_map.anchoredTop ? "below it" : "above it"));
		}
		g_lastX = x;
		g_lastY = y;
		std::scoped_lock l(g_lock);
		g_last = { { "banner_at", { x, y } }, { "banner_size", { bw, bh } }, { "units_per_local", k }, { "want", { wantX, wantY } },
			{ "translation", { g_tx, g_ty } }, { "geometry_mode", g_geometryMode == 0 ? "unknown" : (g_geometryMode == 1 ? "includes translation" : "excludes translation") } };
	}

	bool Placing() { return g_placing; }

	json State()
	{
		std::scoped_lock l(g_lock);
		json j = g_last;
		j["status"] = g_status;
		j["live_candidates"] = g_candidates;
		j["placing"] = g_placing;
		return j;
	}
}
