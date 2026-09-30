#include "Capture.h"

#include "Settings.h"
#include "Ue.h"

namespace capture
{
	namespace
	{
		constexpr const wchar_t* kUiMaterial = L"/Game/Materials/M_LocalMapUI.M_LocalMapUI";
		constexpr const wchar_t* kSobelMaterial = L"/Game/Materials/M_LocalMapSobelEffect.M_LocalMapSobelEffect";
		constexpr const wchar_t* kSecondPass = L"/Game/Materials/RT_LocalMapSecondPass.RT_LocalMapSecondPass";

		Record     g_rec;
		ue::Handle g_mid, g_sobel, g_secondPass;
		RE::TESObjectCELL* g_cell = nullptr;
		bool       g_interior = false;
		ULONGLONG  g_nextCheck = 0, g_lastCapture = 0;
		bool       g_force = true;
		int        g_captures = 0;
		double     g_lastMicros = 0;

		std::mutex  g_lock;
		std::string g_status = "not captured yet";

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_lock);
			if (g_status != a_s) logger::info("capture: {}", a_s);
			g_status = std::move(a_s);
		}

		UE::UObject* ObjectProp(UE::UObject* a_o, const char* a_name)
		{
			auto* cls = a_o ? a_o->GetClass() : nullptr;
			const auto off = cls ? ue::Offset(cls, a_name) : -1;
			auto** p = off >= 0 ? ue::At<UE::UObject*>(a_o, off) : nullptr;
			return p && *p && ue::IsLive(*p) ? *p : nullptr;
		}

		UE::UObject* Cached(ue::Handle& a_h, const wchar_t* a_path)
		{
			if (auto* o = a_h.Get()) return o;
			auto* o = ue::Load(a_path);
			a_h.Set(o);
			return o;
		}

		bool PlayerWorld(UE::UObject* a_pc, std::array<double, 3>& a_out)
		{
			static ue::Getter getPawn(L"K2_GetPawn");
			static ue::Getter location(L"K2_GetActorLocation");
			UE::UObject* pawn = nullptr;
			return a_pc && getPawn.Get(a_pc, pawn) && pawn && ue::IsLive(pawn) && location.Get(pawn, a_out);
		}

		// one capture component's state, saved and put back around our capture
		struct Saved
		{
			std::array<double, 3> loc{}, rot{};
			float        ortho = 0;
			std::uint8_t mode = 0;
			bool         ok = false;
		};

		Saved Save(UE::UObject* a_c)
		{
			Saved s;
			static ue::Getter getLoc(L"K2_GetComponentLocation");
			static ue::Getter getRot(L"K2_GetComponentRotation");
			auto* cls = a_c->GetClass();
			const auto ortho = ue::Offset(cls, "OrthoWidth");
			const auto mode = ue::Offset(cls, "PrimitiveRenderMode");
			if (ortho < 0 || mode < 0 || !getLoc.Get(a_c, s.loc) || !getRot.Get(a_c, s.rot)) return s;
			s.ortho = *ue::At<float>(a_c, ortho);
			s.mode = *ue::At<std::uint8_t>(a_c, mode);
			s.ok = true;
			return s;
		}

		void Place(UE::UObject* a_c, const std::array<double, 3>& a_loc, const std::array<double, 3>& a_rot, float a_ortho, std::uint8_t a_mode)
		{
			ue::Call l(a_c, L"K2_SetWorldLocation");
			l.Set("NewLocation", a_loc);
			l.Set("bTeleport", true);
			l.Run();
			ue::Call r(a_c, L"K2_SetWorldRotation");
			r.Set("NewRotation", a_rot);
			r.Set("bTeleport", true);
			r.Run();
			auto* cls = a_c->GetClass();
			if (auto* o = cls ? ue::At<float>(a_c, ue::Offset(cls, "OrthoWidth")) : nullptr) *o = a_ortho;
			if (auto* m = cls ? ue::At<std::uint8_t>(a_c, ue::Offset(cls, "PrimitiveRenderMode")) : nullptr) *m = a_mode;
		}

		UE::UObject* Material(UE::UObject* a_pc, bool a_exterior)
		{
			auto* mid = g_mid.Get();
			if (!mid) {
				static auto* lib = ue::Class(L"/Script/Engine.KismetMaterialLibrary");
				auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
				auto* parent = ue::Load(kUiMaterial);
				if (!cdo || !parent) return nullptr;
				ue::Call c(cdo, L"CreateDynamicMaterialInstance");
				c.Set("WorldContextObject", a_pc);
				c.Set("Parent", parent);
				c.Run();
				mid = c.Get<UE::UObject*>("ReturnValue");
				g_mid.Set(mid);
				if (mid) logger::info("capture: our instance of the game's local-map material is {}", ue::NameOf(mid));
			}
			if (mid) {
				ue::Call p(mid, L"SetScalarParameterValue");
				if (void* name = p.At("ParameterName")) new (name) UE::FName(L"IsExterior", UE::EFindName::Add);
				p.Set("Value", a_exterior ? 1.0f : 0.0f);
				p.Run();
			}
			return mid;
		}

		bool Capture(UE::UObject* a_pc, const std::array<double, 3>& a_player, bool a_interior)
		{
			const auto& s = settings::Get();
			auto* depth = ObjectProp(a_pc, "LocalMapSceneDepthCaptureComponent");
			auto* color = ObjectProp(a_pc, "LocalMapBaseColorCaptureComponent");
			auto* sobel = Cached(g_sobel, kSobelMaterial);
			auto* second = Cached(g_secondPass, kSecondPass);
			if (!depth || !color) {
				Status("the game's local-map capture components are not on the player controller - nothing drawn");
				return false;
			}
			const Saved d = Save(depth), c = Save(color);
			if (!d.ok || !c.ok) {
				Status("the capture components' transform or width cannot be read - nothing drawn");
				return false;
			}
			const auto t0 = std::chrono::steady_clock::now();
			const double width = std::max(1000.0, s.captureWidthMetres * 100.0);
			const double height = a_interior ? std::max(50.0, s.interiorCutMetres * 100.0) : 20000.0;
			const std::array<double, 3> at{ a_player[0], a_player[1], a_player[2] + height };
			const std::array<double, 3> down{ -90.0, -90.0, 0.0 };   // pitch, yaw, roll - the game's CameraRotationAngles: north at the top
			// PRM_RenderScenePrimitives (1): the game's own show-only list is built by its native map build, not here
			Place(depth, at, down, static_cast<float>(width), 1);
			Place(color, at, down, static_cast<float>(width), 1);
			ue::Call(depth, L"CaptureScene").Run();
			ue::Call(color, L"CaptureScene").Run();
			bool sobelDrawn = false;
			if (sobel && second) {
				static auto* lib = ue::Class(L"/Script/Engine.KismetRenderingLibrary");
				auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
				ue::Call draw(cdo, L"DrawMaterialToRenderTarget");
				if (draw) {
					draw.Set("WorldContextObject", a_pc);
					draw.Set("TextureRenderTarget", second);
					draw.Set("Material", sobel);
					sobelDrawn = draw.Run();
				}
			}
			Place(depth, d.loc, d.rot, d.ortho, d.mode);   // everything back the same frame
			Place(color, c.loc, c.rot, c.ortho, c.mode);
			g_lastMicros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			auto* mid = Material(a_pc, !a_interior);
			g_rec = { mid != nullptr, mid, a_player[0], a_player[1], width };
			++g_captures;
			if (g_captures == 1 || !sobelDrawn) {
				logger::info("capture: #{} {} at ({:.0f}, {:.0f}), {:.0f} m across, camera {:.1f} m above the feet; Sobel pass {}; {:.0f} us on the game thread",
					g_captures, a_interior ? "interior" : "exterior", a_player[0], a_player[1], width / 100.0, height / 100.0,
					sobelDrawn ? "drawn" : "NOT drawn (material or render target missing)", g_lastMicros);
			}
			Status(std::format("drawing the local map around you ({:.0f} m across, {} captures)", width / 100.0, g_captures));
			return mid != nullptr;
		}
	}

	void Tick(bool a_gameplay)
	{
		const auto& s = settings::Get();
		if (!s.alwaysDrawLocalMap || !s.enabled) {
			g_rec.valid = false;
			g_force = true;
			return;
		}
		if (!a_gameplay) return;   // the game's own Map screen captures for itself
		static float lastWidth = -1, lastCut = -1;
		if (s.captureWidthMetres != lastWidth || s.interiorCutMetres != lastCut) {
			lastWidth = s.captureWidthMetres, lastCut = s.interiorCutMetres;
			g_force = true;
		}
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextCheck) return;
		g_nextCheck = now + 250;
		auto* pc = ue::PlayerController();
		auto* player = RE::PlayerCharacter::GetSingleton();
		std::array<double, 3> w{};
		if (!pc || !player || !PlayerWorld(pc, w)) return;
		auto* cell = player->parentCell;
		const bool interior = static_cast<bool>(player->GetInterior());
		const double moved = g_rec.valid ? std::hypot(w[0] - g_rec.cx, w[1] - g_rec.cy) : 1e12;
		const bool need = g_force || !g_rec.valid || cell != g_cell || interior != g_interior || moved > g_rec.width * std::clamp(s.recaptureMoveFraction, 0.05f, 0.45f);
		if (!need) return;
		g_force = false;
		g_cell = cell;
		g_interior = interior;
		g_lastCapture = now;
		Capture(pc, w, interior);
	}

	Record Current() { return g_rec.valid && g_mid.Get() ? g_rec : Record{}; }

	void Invalidate() { g_force = true; }

	json State()
	{
		std::scoped_lock l(g_lock);
		return { { "status", g_status }, { "captures", g_captures }, { "valid", g_rec.valid }, { "centre", { g_rec.cx, g_rec.cy } },
			{ "width_cm", g_rec.width }, { "interior", g_interior }, { "last_capture_us", g_lastMicros } };
	}
}
