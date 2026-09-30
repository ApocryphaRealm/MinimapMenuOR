#include "Popup.h"

#include "Ue.h"

namespace popup
{
	namespace
	{
		struct Follower
		{
			const wchar_t* classPath;
			const char*    name;
			ue::Handle     widget;
			std::vector<ue::Handle> known;   // live instances found by the last scan (checked by their slots)
			ULONGLONG      nextFind = 0, nextScan = 0, nextPass = 0;
			int            candidates = 0;
			bool           translated = false, scaled = false, placing = false;
			double         tx = 0, ty = 0, sc = 1;   // what this mod wrote
			int            geometryMode = 0;          // 0 unknown, 1 the cached geometry includes the render transform, 2 it does not
			double         lastX = 0, lastY = 0;
			// a move written and not yet seen in the cached geometry: the viewport shift it should make, and since when
			// (a stale geometry made every pass add the same correction again - the compass ran to X 440719, 2026-09-30)
			double         pendDX = 0, pendDY = 0;
			ULONGLONG      pendSince = 0;
			Rect           placed;
			std::string    status = "not found yet";
			json           last = json::object();
		};

		Follower g_f[2] = {
			{ L"/Game/UI/Modern/HUD/WBP_ModernHud_Area.WBP_ModernHud_Area_C", "location banner" },
			{ L"/Game/UI/Modern/HUD/Main/Compass/WBP_ModernHud_Compass.WBP_ModernHud_Compass_C", "compass" },
		};
		std::mutex g_lock;

		void Status(Follower& a_f, std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (a_f.status != a_s) logger::info("popup: {}: {}", a_f.name, a_s);
			a_f.status = std::move(a_s);
		}

		// where the widget sits on the viewport (viewport units) and how big, from its cached geometry; a_unitsPerLocal
		// includes every render scale above and on it
		bool Measure(UE::UObject* a_w, double& a_x, double& a_y, double& a_w2, double& a_h, double& a_unitsPerLocal)
		{
			auto* pc = ue::PlayerController();
			static auto* lib = ue::Class(L"/Script/UMG.SlateBlueprintLibrary");
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!a_w || !pc || ue::Dying(pc) || !cdo) return false;
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
				if (!c.RunGuarded()) return false;   // the world-context call: fault-guarded (a quit, a load)
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

		// the laid-out instance: live objects only; the object array only every 10 s, known instances re-measured each second
		UE::UObject* Find(Follower& a_f)
		{
			if (auto* w = a_f.widget.Get()) return w;
			const ULONGLONG now = GetTickCount64();
			if (now < a_f.nextFind) return nullptr;
			a_f.nextFind = now + 1000;
			std::vector<UE::UObject*> all;
			if (now >= a_f.nextScan || a_f.known.empty()) {
				a_f.nextScan = now + 10000;
				auto* cls = ue::Class(a_f.classPath);
				all = cls ? ue::AllOf(cls) : std::vector<UE::UObject*>{};
				a_f.known.clear();
				for (auto* o : all) {
					ue::Handle h;
					h.Set(o);
					a_f.known.push_back(h);
				}
			} else {
				for (const auto& h : a_f.known) {
					if (auto* o = h.Get()) all.push_back(o);
				}
			}
			a_f.candidates = static_cast<int>(all.size());
			UE::UObject* found = nullptr;
			for (auto* w : all) {
				double x = 0, y = 0, bw = 0, bh = 0, k = 0;
				if (Measure(w, x, y, bw, bh, k)) {
					found = w;
					break;
				}
			}
			a_f.widget.Set(found);
			a_f.translated = a_f.scaled = false;
			a_f.tx = a_f.ty = 0.0;
			a_f.sc = 1.0;
			a_f.geometryMode = 0;
			if (found) Status(a_f, std::format("found the laid-out {} ({} of {} live)", a_f.name, ue::NameOf(found), all.size()));
			else Status(a_f, all.empty() ? std::format("no live {} yet", a_f.name) : std::format("{} live, none laid out yet", all.size()));
			return found;
		}

		void Translate(Follower& a_f, UE::UObject* a_w, double a_x, double a_y)
		{
			const double t[2] = { a_x, a_y };
			ue::CallFirst(a_w, L"SetRenderTranslation", t, sizeof(t));
			a_f.tx = a_x;
			a_f.ty = a_y;
			a_f.translated = a_x != 0.0 || a_y != 0.0;
		}

		void Scale(Follower& a_f, UE::UObject* a_w, double a_s)
		{
			const double pivot[2] = { 0.5, 0.0 };   // shrinks towards its top middle
			ue::CallFirst(a_w, L"SetRenderTransformPivot", pivot, sizeof(pivot));
			const double sc[2] = { a_s, a_s };
			ue::CallFirst(a_w, L"SetRenderScale", sc, sizeof(sc));
			a_f.sc = a_s;
			a_f.scaled = a_s != 1.0;
		}
	}

	Rect Tick(Widget a_which, const Rect& a_after, bool a_active, double a_gapUnits, bool a_fit, double a_scale)
	{
		auto& f = g_f[static_cast<int>(a_which)];
		// an inactive follower does not go looking (Find measures through the world, which a quit tears down): only one it
		// still holds is put back
		auto* w = a_active ? Find(f) : f.widget.Get();
		if (!w) {
			f.placing = false;
			return {};
		}
		if (!a_active || !a_after.valid) {
			if (f.translated) Translate(f, w, 0.0, 0.0);
			if (f.scaled) Scale(f, w, 1.0);
			if (f.placing) Status(f, "the game's own place is back");
			f.placing = false;
			return {};
		}
		f.placing = true;
		const ULONGLONG now = GetTickCount64();
		if (now < f.nextPass) return f.placed;
		f.nextPass = now + 200;   // five times a second
		double x = 0, y = 0, bw = 0, bh = 0, k = 1;
		if (!Measure(w, x, y, bw, bh, k)) {
			return f.placed;   // not laid out right now (the banner shows only when an area is entered): the last place stands
		}
		// learn once whether the cached geometry already includes the render transform (it moved after our write)
		if (f.geometryMode == 0 && f.translated) {
			const double moved = std::hypot(x - f.lastX, y - f.lastY);
			f.geometryMode = moved > 1.0 ? 1 : 2;
			logger::info("popup: {}: the cached geometry {} the render transform", f.name, f.geometryMode == 1 ? "includes" : "does not include");
		}
		// the widget's own size and scale without this mod's scale; the size it should have now
		const double kParent = k / std::max(0.01, f.sc);           // viewport units per local unit, before our scale
		const double natural = bw / std::max(0.01, f.sc);          // its width at scale 1
		// the size setting applies ON TOP of the fit: fitted, 1.00 is the minimap's width (the owner, 2026-09-29: "The compass
		// size in the minimap menu isn't responsive" - the fit used to cap the size, so any setting above the fitted scale
		// did nothing, and with a wide compass that was every setting above about 0.4)
		double want = std::clamp(a_scale, 0.1, 3.0);
		if (a_which == Widget::kCompass) {
			// the compass' visible bar is a share of its widget box: the owner's setting of 2026-09-30 (fCompassScale 2.65
			// with the fit on - "whatever I currently have should be set as the true boundary") put the bar exactly on the
			// minimap's width, so the bar is 1 / 2.65 of the box. Fitted, the BAR is sized to the minimap (up or down), and
			// 1.00 is that width.
			constexpr double kBarShare = 1.0 / 2.65;
			if (a_fit) want *= a_after.w / (natural * kBarShare);
		} else if (a_fit && natural > a_after.w) {
			want *= a_after.w / natural;   // the banner: its text only ever shrinks to the minimap's width
		}
		want = std::max(0.1, want);
		if (std::abs(want - f.sc) > 0.005) {
			Scale(f, w, want);
			return f.placed;   // measured again on the next pass, at the new size
		}
		const double wantX = a_after.x + a_after.w * 0.5 - bw * 0.5;
		const double wantY = a_after.anchoredTop ? a_after.y + a_after.h + a_gapUnits : a_after.y - a_gapUnits - bh;
		// a translation this far out is a runaway: back to the game's own place, and the geometry is learned again
		constexpr double kRunaway = 8000.0;
		if (std::abs(f.tx) > kRunaway || std::abs(f.ty) > kRunaway) {
			logger::warn("popup: {}: translation ({:.0f}, {:.0f}) ran away - put back to the game's place", f.name, f.tx, f.ty);
			Translate(f, w, 0.0, 0.0);
			f.geometryMode = 0;
			f.pendDX = f.pendDY = 0.0;
			f.pendSince = 0;
			return f.placed;
		}
		// the last move not yet in the cached geometry: no new correction on top of it (it would be the same one again)
		if (f.pendSince != 0) {
			const double seenX = x - f.lastX, seenY = y - f.lastY;
			const bool shown = std::hypot(seenX - f.pendDX, seenY - f.pendDY) < 0.5 * std::hypot(f.pendDX, f.pendDY) + 1.0;
			if (!shown && f.geometryMode != 2) {
				if (now - f.pendSince < 2000) return f.placed;
				logger::info("popup: {}: a move did not show in the cached geometry within 2 s - learned again", f.name);
				f.geometryMode = 0;
			}
			f.pendDX = f.pendDY = 0.0;
			f.pendSince = 0;
		}
		// the geometry includes the translation (the case seen in game): move by the difference; otherwise from the base
		const double tx = f.geometryMode == 2 ? (wantX - x) / kParent : f.tx + (wantX - x) / kParent;
		const double ty = f.geometryMode == 2 ? (wantY - y) / kParent : f.ty + (wantY - y) / kParent;
		if (std::abs(tx - f.tx) > 0.5 || std::abs(ty - f.ty) > 0.5) {
			f.pendDX = (tx - f.tx) * kParent;
			f.pendDY = (ty - f.ty) * kParent;
			f.pendSince = now;
			Translate(f, w, tx, ty);
			Status(f, std::format("following the minimap ({})", a_after.anchoredTop ? "below" : "above"));
		}
		f.lastX = x;
		f.lastY = y;
		f.placed = { wantX, wantY, bw, bh, true, a_after.anchoredTop };
		std::scoped_lock l(g_lock);
		f.last = { { "at", { x, y } }, { "size", { bw, bh } }, { "scale", f.sc }, { "want", { wantX, wantY } }, { "translation", { f.tx, f.ty } },
			{ "geometry_mode", f.geometryMode == 0 ? "unknown" : (f.geometryMode == 1 ? "includes the transform" : "excludes the transform") } };
		return f.placed;
	}

	bool Placing(Widget a_which) { return g_f[static_cast<int>(a_which)].placing; }

	json State()
	{
		std::scoped_lock l(g_lock);
		json j = json::object();
		for (auto& f : g_f) {
			json e = f.last;
			e["status"] = f.status;
			e["live_candidates"] = f.candidates;
			e["placing"] = f.placing;
			j[f.name] = e;
		}
		return j;
	}
}
