#include "Minimap.h"

#include "Capture.h"
#include "Controls.h"
#include "LocalMap.h"
#include "Popup.h"
#include "Settings.h"
#include "Ue.h"

#include <map>

namespace minimap
{
	namespace
	{
		constexpr const wchar_t* kPaper = L"/Game/ArtOriginal/textures/menus/map/local/T_mappaper01.T_mappaper01";
		constexpr const wchar_t* kPlayerArrow = L"/Game/Art/UI/Modern/GameMenuLayer/Map/T_Map_Player_Location.T_Map_Player_Location";
		constexpr const wchar_t* kMarkerFill = L"/Game/Art/UI/Modern/GameMenuLayer/Map/T_Map_Marker_Filling.T_Map_Marker_Filling";
		constexpr const wchar_t* kHudViewModel = L"/Script/Altar.VHUDMainViewModel";

		// EModernMarkerType (None, Camp, Cave, City, Elvenruin, Fortruin, Mine, Mountain, Tavern, Settlement,
		// DaedrickShrine, OblivionGate, Door) -> the game's own map-icon material (DTModern_AltarMapIconDesignTable)
		constexpr const wchar_t* kTypeIcon[13] = {
			nullptr,
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Camp.MIC_UI_MapIcon_Camp",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Cave.MIC_UI_MapIcon_Cave",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_City.MIC_UI_MapIcon_City",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Ruins.MIC_UI_MapIcon_Ruins",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Fort.MIC_UI_MapIcon_Fort",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Mines.MIC_UI_MapIcon_Mines",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Peaks.MIC_UI_MapIcon_Peaks",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Inn.MIC_UI_MapIcon_Inn",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Settlement.MIC_UI_MapIcon_Settlement",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Shrines.MIC_UI_MapIcon_Shrines",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Gates.MIC_UI_MapIcon_Gates",
			L"/Game/UI/Materials/Map/MIC_UI_MapIcon_Wayshrines.MIC_UI_MapIcon_Wayshrines",
		};
		constexpr int    kKindQuest = 100, kKindHostile = 101;
		constexpr int    kPool = 48;
		constexpr double kBaseSize = 400.0;   // the map's side at scale 1, in viewport units

		using Clock = std::chrono::steady_clock;

		struct Slot
		{
			ue::Handle image, slot;
			int        kind = -1;
			bool       dim = false;
			bool       shown = false;
			double     x = -1e9, y = -1e9, size = 0;
		};

		// ---- the widget (game thread) ----
		ue::Handle g_root, g_panel, g_panelSlot, g_bg, g_arrow, g_arrowSlot, g_frame, g_mapImg, g_mapSlot;
		UE::UObject* g_mapShownMaterial = nullptr;   // the material the local-map image shows now (compared, never read)
		bool         g_mapImgShown = false;
		std::array<Slot, kPool> g_icons;
		int       g_builds = 0;
		ULONGLONG g_lastBuildTry = 0;
		bool      g_rootShown = false;
		bool      g_shownRuntime = true;
		bool      g_zoomIn = false;
		double    g_panX = 0.0, g_panY = 0.0;
		Clock::time_point g_last{};

		// what the last layout applied (re-applied only when something changes)
		struct Layout
		{
			int    anchor = -1, shape = -1, image = -1;
			double posX = 0, posY = 0, size = 0, opacity = -1;
		} g_layout;
		popup::Rect g_rect;
		double      g_viewportW = 0, g_viewportH = 0, g_dpi = 1;
		ULONGLONG   g_nextLayout = 0, g_nextMarkers = 0;

		std::map<int, ue::Handle> g_iconAssets;
		ue::Handle g_fillTexture, g_arrowTexture;

		// ---- shared with other threads ----
		std::mutex         g_queueLock;
		std::vector<Action> g_queue;
		std::atomic<bool>  g_ownsPopup{ false }, g_ownsCompass{ false };
		std::mutex         g_stateLock;
		json               g_state = json::object();
		std::string        g_status = "not built";

		void Status(std::string a_s)
		{
			std::scoped_lock l(g_stateLock);
			if (g_status != a_s) logger::info("minimap: {}", a_s);
			g_status = std::move(a_s);
		}

		// ---- small reflected calls ----
		void SetVisible(UE::UObject* a_w, bool a_visible)
		{
			if (!a_w) return;
			ue::Call c(a_w, L"SetVisibility");
			c.Set("InVisibility", static_cast<std::uint8_t>(a_visible ? 3 : 1));   // HitTestInvisible (drawn, never takes the mouse) / Collapsed
			c.Run();
		}

		void Vec2(UE::UObject* a_o, const wchar_t* a_fn, double a_x, double a_y)
		{
			const double v[2] = { a_x, a_y };
			ue::CallFirst(a_o, a_fn, v, sizeof(v));
		}

		void Anchors(UE::UObject* a_slot, double a_minX, double a_minY, double a_maxX, double a_maxY)
		{
			const double a[4] = { a_minX, a_minY, a_maxX, a_maxY };   // FAnchors: Minimum, Maximum (FVector2D, doubles)
			ue::CallFirst(a_slot, L"SetAnchors", a, sizeof(a));
		}

		void Colour(UE::UObject* a_image, float a_r, float a_g, float a_b, float a_a)
		{
			const float c[4] = { a_r, a_g, a_b, a_a };   // FLinearColor
			ue::CallFirst(a_image, L"SetColorAndOpacity", c, sizeof(c));
		}

		void Float(UE::UObject* a_o, const wchar_t* a_fn, float a_v) { ue::CallFirst(a_o, a_fn, &a_v, sizeof(a_v)); }

		bool BrushFromTexture(UE::UObject* a_image, UE::UObject* a_texture)
		{
			if (!a_image || !a_texture) return false;
			ue::Call c(a_image, L"SetBrushFromTexture");
			c.Set("Texture", a_texture);
			c.Set("bMatchSize", false);
			return c.Run();
		}

		bool BrushFromMaterial(UE::UObject* a_image, UE::UObject* a_material)
		{
			if (!a_image || !a_material) return false;
			ue::Call c(a_image, L"SetBrushFromMaterial");
			c.Set("Material", a_material);
			return c.Run();
		}

		UE::UObject* Asset(ue::Handle& a_cache, const wchar_t* a_path)
		{
			if (auto* o = a_cache.Get()) return o;
			auto* o = ue::Load(a_path);
			a_cache.Set(o);
			return o;
		}

		UE::UObject* Create(const wchar_t* a_classPath)
		{
			static auto* lib = ue::Class(L"/Script/UMG.WidgetBlueprintLibrary");
			auto* cls = ue::Class(a_classPath);
			auto* pc = ue::PlayerController();
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!cdo || !cls || !pc) return nullptr;
			ue::Call c(cdo, L"Create");
			c.Set("WorldContextObject", pc);
			c.Set("WidgetType", cls);
			c.Set("OwningPlayer", pc);
			c.Run();
			return c.Get<UE::UObject*>("ReturnValue");
		}

		// the brush's draw type: an image, or (the circle) a rounded box with the half-height radius - Slate draws the
		// brush's resource inside the rounded box
		bool ApplyShape(UE::UObject* a_image, bool a_circle, double a_size)
		{
			static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
			static auto* outlineStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrushOutlineSettings"));
			if (!a_image || !brushStruct || !outlineStruct) return false;
			auto*      cls = a_image->GetClass();
			const auto brushOff = ue::Offset(cls, "Brush");
			const auto brushSize = ue::SizeOf(cls, "Brush");
			const auto drawAs = ue::Offset(brushStruct, "DrawAs");
			const auto outline = ue::Offset(brushStruct, "OutlineSettings");
			const auto radii = ue::Offset(outlineStruct, "CornerRadii");
			const auto radiiSize = ue::SizeOf(outlineStruct, "CornerRadii");
			const auto rounding = ue::Offset(outlineStruct, "RoundingType");
			const auto width = ue::Offset(outlineStruct, "Width");
			if (brushOff < 0 || brushSize <= 0 || drawAs < 0 || outline < 0 || radii < 0 || rounding < 0 || width < 0) return false;
			auto* b = reinterpret_cast<std::uint8_t*>(a_image) + brushOff;
			b[drawAs] = a_circle ? 4 : 3;   // ESlateBrushDrawType: RoundedBox / Image
			if (a_circle) {
				const double r = a_size * 0.5;
				if (radiiSize == 32) {
					const double v[4] = { r, r, r, r };
					std::memcpy(b + outline + radii, v, sizeof(v));
				} else if (radiiSize == 16) {
					const float v[4] = { static_cast<float>(r), static_cast<float>(r), static_cast<float>(r), static_cast<float>(r) };
					std::memcpy(b + outline + radii, v, sizeof(v));
				}
				b[outline + rounding] = 1;   // ESlateBrushRoundingType::HalfHeightRadius
				const float w = 0.0f;
				std::memcpy(b + outline + width, &w, sizeof(w));
			}
			std::vector<std::uint8_t> copy(b, b + brushSize);
			return ue::CallFirst(a_image, L"SetBrush", copy.data(), copy.size());
		}

		// the frame (the owner: "The minimap needs to have some sort of frame to go around it"): a rounded box with no
		// fill and an outline in the game's map-ink brown, square or round with the map
		bool ApplyFrame(UE::UObject* a_image, bool a_circle, double a_size)
		{
			static auto* brushStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrush"));
			static auto* outlineStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateBrushOutlineSettings"));
			static auto* colorStruct = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/SlateCore.SlateColor"));
			if (!a_image || !brushStruct || !outlineStruct || !colorStruct) return false;
			auto*      cls = a_image->GetClass();
			const auto brushOff = ue::Offset(cls, "Brush");
			const auto brushSize = ue::SizeOf(cls, "Brush");
			const auto drawAs = ue::Offset(brushStruct, "DrawAs");
			const auto tint = ue::Offset(brushStruct, "TintColor");
			const auto outline = ue::Offset(brushStruct, "OutlineSettings");
			const auto radii = ue::Offset(outlineStruct, "CornerRadii");
			const auto radiiSize = ue::SizeOf(outlineStruct, "CornerRadii");
			const auto oColor = ue::Offset(outlineStruct, "Color");
			const auto rounding = ue::Offset(outlineStruct, "RoundingType");
			const auto width = ue::Offset(outlineStruct, "Width");
			const auto specified = ue::Offset(colorStruct, "SpecifiedColor");
			const auto rule = ue::Offset(colorStruct, "ColorUseRule");
			if (brushOff < 0 || brushSize <= 0 || drawAs < 0 || tint < 0 || outline < 0 || radii < 0 || oColor < 0 || rounding < 0 || width < 0 ||
				specified < 0 || rule < 0) {
				return false;
			}
			auto* b = reinterpret_cast<std::uint8_t*>(a_image) + brushOff;
			b[drawAs] = 4;   // RoundedBox
			const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			std::memcpy(b + tint + specified, clear, sizeof(clear));
			b[tint + rule] = 0;   // UseColor_Specified
			const float ink[4] = { 0.29f, 0.196f, 0.133f, 1.0f };   // #4a3222, the paper-map brown of our Oblivion Remastered banners
			std::memcpy(b + outline + oColor + specified, ink, sizeof(ink));
			b[outline + oColor + rule] = 0;
			const double r = a_circle ? a_size * 0.5 : 4.0;
			if (radiiSize == 32) {
				const double v[4] = { r, r, r, r };
				std::memcpy(b + outline + radii, v, sizeof(v));
			} else if (radiiSize == 16) {
				const float v[4] = { static_cast<float>(r), static_cast<float>(r), static_cast<float>(r), static_cast<float>(r) };
				std::memcpy(b + outline + radii, v, sizeof(v));
			}
			b[outline + rounding] = a_circle ? 1 : 0;   // HalfHeightRadius / FixedRadius
			const float w = 4.0f;
			std::memcpy(b + outline + width, &w, sizeof(w));
			std::vector<std::uint8_t> copy(b, b + brushSize);
			return ue::CallFirst(a_image, L"SetBrush", copy.data(), copy.size());
		}

		UE::UObject* NewImage(UE::UObject* a_tree, UE::UObject* a_panel, const wchar_t* a_name, UE::UObject** a_slot)
		{
			static auto* imageClass = ue::Class(L"/Script/UMG.Image");
			if (!imageClass || !a_tree || !a_panel) return nullptr;
			auto* img = UE::NewObject<UE::UObject>(a_tree, imageClass, UE::FName(a_name));
			if (!img) return nullptr;
			ue::Call add(a_panel, L"AddChildToCanvas");
			add.Set("Content", img);
			add.Run();
			*a_slot = add.Get<UE::UObject*>("ReturnValue");
			return *a_slot ? img : nullptr;
		}

		bool Build()
		{
			const ULONGLONG now = GetTickCount64();
			if (g_lastBuildTry && now - g_lastBuildTry < 2000) return false;   // rule 17, at most every 2 s
			g_lastBuildTry = now;
			auto* treeClass = ue::Class(L"/Script/UMG.WidgetTree");
			auto* canvasClass = ue::Class(L"/Script/UMG.CanvasPanel");
			auto* root = Create(L"/Script/UMG.UserWidget");
			if (!root || !treeClass || !canvasClass) {
				Status("cannot be built yet (no player controller)");
				return false;
			}
			++g_builds;
			const std::wstring n = std::to_wstring(g_builds);
			auto** tree = root->GetClass() ? ue::At<UE::UObject*>(root, ue::Offset(root->GetClass(), "WidgetTree")) : nullptr;
			if (tree && !*tree) *tree = UE::NewObject<UE::UObject>(root, treeClass, UE::FName((L"MinimapMenuTree" + n).c_str()));
			auto* canvas = tree && *tree ? UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName((L"MinimapMenuCanvas" + n).c_str())) : nullptr;
			auto** rootWidget = tree && *tree ? ue::At<UE::UObject*>(*tree, ue::Offset(treeClass, "RootWidget")) : nullptr;
			if (!canvas || !rootWidget) {
				Status("its canvas could not be made");
				return false;
			}
			*rootWidget = canvas;
			auto* panel = UE::NewObject<UE::UObject>(*tree, canvasClass, UE::FName((L"MinimapMenuPanel" + n).c_str()));
			if (!panel) {
				Status("the map panel could not be made");
				return false;
			}
			ue::Call add(canvas, L"AddChildToCanvas");
			add.Set("Content", panel);
			add.Run();
			auto* panelSlot = add.Get<UE::UObject*>("ReturnValue");
			if (!panel || !panelSlot) {
				Status("the map panel could not be made");
				return false;
			}
			const std::uint8_t clip = 1;   // EWidgetClipping::ClipToBounds: markers and the image stay inside the frame
			ue::CallFirst(panel, L"SetClipping", &clip, sizeof(clip));
			const bool no = false;
			ue::CallFirst(panelSlot, L"SetAutoSize", &no, sizeof(no));

			UE::UObject* bgSlot = nullptr;
			auto*        bg = NewImage(*tree, panel, (L"MinimapMenuImage" + n).c_str(), &bgSlot);
			if (!bg) {
				Status("the map image could not be made");
				return false;
			}
			Anchors(bgSlot, 0, 0, 1, 1);   // fills the panel
			const float margin[4] = { 0, 0, 0, 0 };
			ue::CallFirst(bgSlot, L"SetOffsets", margin, sizeof(margin));

			// the game's own local map, between the parchment and the markers (LocalMap.h)
			UE::UObject* mapSlot = nullptr;
			auto*        mapImg = NewImage(*tree, panel, (L"MinimapMenuLocalMap" + n).c_str(), &mapSlot);
			if (mapImg) {
				Anchors(mapSlot, 0, 0, 0, 0);
				ue::CallFirst(mapSlot, L"SetAutoSize", &no, sizeof(no));
				SetVisible(mapImg, false);
			}
			g_mapImg.Set(mapImg);
			g_mapSlot.Set(mapSlot);
			g_mapShownMaterial = nullptr;
			g_mapImgShown = false;

			for (int i = 0; i < kPool; ++i) {
				UE::UObject* s = nullptr;
				auto* img = NewImage(*tree, panel, (L"MinimapMenuIcon" + n + L"_" + std::to_wstring(i)).c_str(), &s);
				g_icons[static_cast<std::size_t>(i)] = {};
				if (!img) continue;
				Anchors(s, 0, 0, 0, 0);
				Vec2(s, L"SetAlignment", 0.5, 0.5);
				ue::CallFirst(s, L"SetAutoSize", &no, sizeof(no));
				SetVisible(img, false);
				g_icons[static_cast<std::size_t>(i)].image.Set(img);
				g_icons[static_cast<std::size_t>(i)].slot.Set(s);
			}

			UE::UObject* arrowSlot = nullptr;
			auto*        arrow = NewImage(*tree, panel, (L"MinimapMenuPlayer" + n).c_str(), &arrowSlot);
			if (arrow) {
				Anchors(arrowSlot, 0.5, 0.5, 0.5, 0.5);
				Vec2(arrowSlot, L"SetAlignment", 0.5, 0.5);
				ue::CallFirst(arrowSlot, L"SetAutoSize", &no, sizeof(no));
				Vec2(arrow, L"SetRenderTransformPivot", 0.5, 0.5);
				BrushFromTexture(arrow, Asset(g_arrowTexture, kPlayerArrow));
			}

			// the frame last, so it draws over the image and the markers at the edge
			UE::UObject* frameSlot = nullptr;
			auto*        frame = NewImage(*tree, panel, (L"MinimapMenuFrame" + n).c_str(), &frameSlot);
			if (frame) {
				Anchors(frameSlot, 0, 0, 1, 1);
				ue::CallFirst(frameSlot, L"SetOffsets", margin, sizeof(margin));
			}

			ue::Call vp(root, L"AddToViewport");
			vp.Set<std::int32_t>("ZOrder", 3);   // under the game's menus
			vp.Run();
			g_root.Set(root);
			g_panel.Set(panel);
			g_panelSlot.Set(panelSlot);
			g_bg.Set(bg);
			g_arrow.Set(arrow);
			g_arrowSlot.Set(arrowSlot);
			g_frame.Set(frame);
			g_layout = {};
			g_rootShown = true;
			SetVisible(root, false);
			g_rootShown = false;
			g_shownRuntime = settings::Get().showOnGameStart;
			Status(std::format("built (build {}; {} icon slots; on the viewport)", g_builds, kPool));
			return true;
		}

		// DEM's positioning (MiniMap.cpp ApplyDisplaySettingsOnce / GetMaxScale), in UMG terms
		void Layout()
		{
			auto* pc = ue::PlayerController();
			auto* panelSlot = g_panelSlot.Get();
			auto* bg = g_bg.Get();
			static auto* lib = ue::Class(L"/Script/UMG.WidgetLayoutLibrary");
			auto* cdo = lib ? lib->GetDefaultObject(false) : nullptr;
			if (!pc || !panelSlot || !bg || !cdo) return;
			ue::Call size(cdo, L"GetViewportSize");
			size.Set("WorldContextObject", pc);
			size.Run();
			const auto px = size.Get<std::array<double, 2>>("ReturnValue");
			ue::Call scale(cdo, L"GetViewportScale");
			scale.Set("WorldContextObject", pc);
			scale.Run();
			const double dpi = std::max(0.1, static_cast<double>(scale.Get<float>("ReturnValue")));
			if (px[0] <= 0 || px[1] <= 0) return;
			g_viewportW = px[0] / dpi;
			g_viewportH = px[1] / dpi;
			g_dpi = dpi;

			const auto& s = settings::Get();
			const int   corner = std::clamp(s.anchor, 0, 3);
			const bool  atRight = corner == 1 || corner == 3;
			const bool  atBottom = corner == 2 || corner == 3;
			// a quarter of the screen: half its width and half its height
			const double cap = std::min(g_viewportW, g_viewportH) * 0.5;
			const double side = std::clamp(kBaseSize * s.scale, 40.0, std::max(40.0, cap));
			const double posX = s.offsetX[corner] / dpi;   // screen pixels -> slot units; +x right, +y down at every corner
			const double posY = s.offsetY[corner] / dpi;
			const int    image = std::clamp(s.mapImage, 0, 1);
			const int    shape = std::clamp(s.shape, 0, 1);

			if (corner != g_layout.anchor) {
				const double ax = atRight ? 1.0 : 0.0, ay = atBottom ? 1.0 : 0.0;
				Anchors(panelSlot, ax, ay, ax, ay);
				Vec2(panelSlot, L"SetAlignment", ax, ay);   // the frame's own corner sits on the screen's
			}
			if (corner != g_layout.anchor || posX != g_layout.posX || posY != g_layout.posY) Vec2(panelSlot, L"SetPosition", posX, posY);
			if (side != g_layout.size) Vec2(panelSlot, L"SetSize", side, side);
			if (s.opacity != g_layout.opacity) {
				if (auto* panel = g_panel.Get()) Float(panel, L"SetRenderOpacity", s.opacity);
			}
			if (image != g_layout.image) {
				// the parchment is always the ground; the game's local map (image 1) is drawn over it where it exists
				static ue::Handle paper;
				const bool ok = BrushFromTexture(bg, Asset(paper, kPaper));
				logger::info("minimap: map picture = {} (parchment {})", image == 1 ? "the game's local map where there is one, else parchment" : "parchment",
					ok ? "set" : "NOT FOUND");
				g_layout.shape = -1;   // a new brush: its draw type is applied again below
			}
			if (shape != g_layout.shape || side != g_layout.size) {
				const bool framed = ApplyFrame(g_frame.Get(), shape == 1, side);
				static bool frameLogged = false;
				if (!frameLogged) {
					frameLogged = true;
					logger::info("minimap: frame {}", framed ? "set (a brown outline)" : "could not be set - the brush's outline settings are not reachable");
				}
				const bool ok = ApplyShape(bg, shape == 1, side);
				static int logged = -1;
				if (logged != shape) {
					logged = shape;
					logger::info("minimap: shape = {} ({})", shape == 1 ? "circle" : "square", ok ? "brush set" : "the brush could not be reached");
				}
			}
			const bool changed = corner != g_layout.anchor || side != g_layout.size || posX != g_layout.posX || posY != g_layout.posY;
			g_layout = { corner, shape, image, posX, posY, side, s.opacity };

			g_rect.w = g_rect.h = side;
			g_rect.x = (atRight ? g_viewportW : 0.0) + posX - (atRight ? side : 0.0);
			g_rect.y = (atBottom ? g_viewportH : 0.0) + posY - (atBottom ? side : 0.0);
			g_rect.anchoredTop = !atBottom;
			g_rect.valid = true;
			if (changed) {
				logger::info("minimap: layout - corner {}, offset ({:.0f}, {:.0f}) px, side {:.0f} (cap {:.0f}), viewport {:.0f}x{:.0f} units at DPI {:.2f} -> ({:.0f}, {:.0f})",
					corner, s.offsetX[corner], s.offsetY[corner], side, cap, g_viewportW, g_viewportH, dpi, g_rect.x, g_rect.y);
			}
		}

		// ---- markers: the HUD compass' own list, plotted around the player ----
		struct MarkerLayout
		{
			bool         ok = false;
			std::int32_t arr = -1, hostileArr = -1, elem = 0, hostileElem = 0;
			std::int32_t angle = -1, distance = -1, type = -1, visible = -1, door = -1, quest = -1, player = -1;
			std::int32_t hAngle = -1, hDistance = -1;
		} g_ml;

		struct RawMarker
		{
			float angle, distance;
			int   type;
			bool  visible, door, quest, player;
		};

		ue::Handle g_vm;
		ULONGLONG  g_nextVmFind = 0;
		float      g_heading = 0.0f;
		std::vector<RawMarker> g_sample;
		int        g_drawn = 0, g_listed = 0, g_hostiles = 0;

		// Enemies from their world positions (the owner, 2026-09-30: "The enemy markers kind of float around the screen a bit
		// when rotating"). The compass' HostileData is a distance and an angle only, and that angle does not keep to the map's
		// frame while the view turns (the location markers, from CompassIconMarkers, held still). So while the compass
		// reports enemies, the paired pawns whose reference is in combat with the player are found (a whole object-array
		// scan, at most twice a second, only while the compass lists any) and each is placed from its own position - on
		// the same rotation, quarter turns and mirror as the map image, so a dot stays on its spot of the map.
		std::vector<ue::Handle> g_hostilePawns;
		ULONGLONG               g_nextHostileScan = 0;
		std::string             g_hostileSource = "none";

		RE::Actor* ActorOf(UE::UObject* a_pawn)
		{
			static UE::UClass*  s_class = nullptr;
			static std::int32_t s_off = -1;
			auto* cls = a_pawn ? a_pawn->GetClass() : nullptr;
			if (!cls) return nullptr;
			if (cls != s_class) {
				s_class = cls;
				s_off = ue::Offset(cls, "TESRefComponent");
			}
			if (s_off < 0) return nullptr;
			auto* comp = *reinterpret_cast<UE::UObject* const*>(reinterpret_cast<const std::uint8_t*>(a_pawn) + s_off);
			if (!comp || !ue::IsLive(comp)) return nullptr;
			const auto id = reinterpret_cast<UE::UVTESObjectRefComponent*>(comp)->formIDInstance;
			auto* form = id ? RE::TESForm::LookupByID(id) : nullptr;
			if (!form) return nullptr;
			const auto t = form->GetFormType();
			return t == RE::FormType::ActorCharacter || t == RE::FormType::ActorCreature ? static_cast<RE::Actor*>(form) : nullptr;
		}

		void ScanHostilePawns()
		{
			g_hostilePawns.clear();
			static auto* pawnClass = ue::Class(L"/Script/Altar.VPairedPawn");
			auto*        player = RE::PlayerCharacter::GetSingleton();
			if (!pawnClass || !player) return;
			for (auto* p : ue::AllOf(pawnClass)) {
				auto* actor = ActorOf(p);
				if (!actor || actor == player || actor->IsDead(false)) continue;
				if (actor->IsInCombat(false) && actor->GetCombatTarget() == player) {
					ue::Handle h;
					h.Set(p);
					g_hostilePawns.push_back(h);
				}
			}
		}

		UE::UObject* ViewModel()
		{
			if (auto* vm = g_vm.Get()) return vm;
			const ULONGLONG now = GetTickCount64();
			if (now < g_nextVmFind) return nullptr;
			g_nextVmFind = now + 2000;
			auto* cls = ue::Class(kHudViewModel);
			auto* vm = cls ? ue::FirstOf(cls) : nullptr;
			g_vm.Set(vm);
			if (vm && !g_ml.ok) {
				auto* ms = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.CompassIconMarker"));
				auto* hs = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.HostileData"));
				g_ml.arr = ue::Offset(cls, "CompassIconMarkers");
				g_ml.hostileArr = ue::Offset(cls, "HostileData");
				if (ms) {
					g_ml.elem = ms->propertiesSize;
					g_ml.angle = ue::Offset(ms, "Angle");
					g_ml.distance = ue::Offset(ms, "Distance");
					g_ml.type = ue::Offset(ms, "Type");
					g_ml.visible = ue::Offset(ms, "bIsVisible");
					g_ml.door = ue::Offset(ms, "bIsThroughLoadDoor");
					g_ml.quest = ue::Offset(ms, "bIsAQuestTarget");
					g_ml.player = ue::Offset(ms, "bIsThePlayerMarker");
				}
				if (hs) {
					g_ml.hostileElem = hs->propertiesSize;
					g_ml.hAngle = ue::Offset(hs, "Angle");
					g_ml.hDistance = ue::Offset(hs, "Distance");
				}
				g_ml.ok = g_ml.arr >= 0 && g_ml.elem > 0 && g_ml.angle >= 0 && g_ml.distance >= 0;
				logger::info("minimap: HUD view model {} - CompassIconMarkers at 0x{:X} ({} bytes each: Angle 0x{:X}, Distance 0x{:X}, Type 0x{:X}, visible 0x{:X}, door 0x{:X}, quest 0x{:X}, player 0x{:X}), HostileData at 0x{:X} ({} bytes)",
					ue::NameOf(vm), g_ml.arr, g_ml.elem, g_ml.angle, g_ml.distance, g_ml.type, g_ml.visible, g_ml.door, g_ml.quest, g_ml.player,
					g_ml.hostileArr, g_ml.hostileElem);
			}
			return vm;
		}

		void Place(Slot& a_s, int a_kind, double a_x, double a_y, double a_size, bool a_dim = false)
		{
			auto* img = a_s.image.Get();
			auto* slot = a_s.slot.Get();
			if (!img || !slot) return;
			if (a_kind != a_s.kind || a_dim != a_s.dim) {
				a_s.kind = -1;   // the colour below is set again with the brush
				a_s.dim = a_dim;
			}
			if (a_kind != a_s.kind) {
				bool ok = false;
				if (a_kind == kKindQuest || a_kind == kKindHostile) {
					ok = BrushFromTexture(img, Asset(g_fillTexture, kMarkerFill));
					if (a_kind == kKindQuest) Colour(img, 1.0f, 0.78f, 0.22f, 1.0f);
					else Colour(img, 0.85f, 0.12f, 0.1f, 1.0f);
				} else if (a_kind > 0 && a_kind < 13) {
					ok = BrushFromMaterial(img, Asset(g_iconAssets[a_kind], kTypeIcon[a_kind]));
					Colour(img, 1.0f, 1.0f, 1.0f, a_dim ? 0.55f : 1.0f);
				}
				a_s.kind = ok ? a_kind : -2;
			}
			if (a_s.kind < 0) return;
			if (std::abs(a_x - a_s.x) > 0.25 || std::abs(a_y - a_s.y) > 0.25) Vec2(slot, L"SetPosition", a_x, a_y);
			if (a_size != a_s.size) Vec2(slot, L"SetSize", a_size, a_size);
			a_s.x = a_x, a_s.y = a_y, a_s.size = a_size;
			if (!a_s.shown) {
				SetVisible(img, true);
				a_s.shown = true;
			}
		}

		void Markers(double a_dt)
		{
			(void)a_dt;
			auto* vm = ViewModel();
			const auto& s = settings::Get();
			static ue::Getter heading(L"GetCompassDirectionValue");
			float h = 0.0f;
			if (vm && heading.Get(vm, h)) g_heading = h;

			const double side = g_layout.size;
			const double half = side * 0.5;
			const double zoom = std::max(0.25f, g_zoomIn ? s.zoomZoomedIn : s.zoomDefault);
			auto*        playerNow = RE::PlayerCharacter::GetSingleton();
			const bool   indoors = playerNow && static_cast<bool>(playerNow->GetInterior());
			const double radiusCm = std::max(100.0, (indoors ? s.radiusInteriorMetres : s.radiusMetres) * 100.0 / zoom);
			const double pxPerCm = half / radiusCm;
			const double up = s.followCameraRotation ? g_heading : 0.0;
			const bool   circle = g_layout.shape == 1;
			const double cx = half + g_panX, cy = half + g_panY;

			// the game's own local map around the player: drawn by the minimap itself (Capture.h), or read from the Map
			// screen where the player opened it (LocalMap.h)
			if (auto* mapImg = g_mapImg.Get()) {
				localmap::Placement lp;
				const auto cap = capture::Current();
				if (s.mapImage == 1 && s.alwaysDrawLocalMap && cap.valid) {
					std::array<double, 3> w{};
					static ue::Getter getPawn(L"K2_GetPawn");
					static ue::Getter location(L"K2_GetActorLocation");
					UE::UObject* pawn = nullptr;
					auto* pc = ue::PlayerController();
					if (pc && getPawn.Get(pc, pawn) && pawn && ue::IsLive(pawn) && location.Get(pawn, w)) {
						// the capture looks straight down with north at the top and east to the right (the game's
						// CameraRotationAngles); Unreal's +Y is south, so down the image
						double u = (w[0] - cap.cx) / cap.width, v = (w[1] - cap.cy) / cap.width;   // from the centre
						for (int k = 0; k < (s.mapQuarterTurns & 3); ++k) {   // a correction: quarter turns clockwise
							const double t = u;
							u = -v, v = t;
						}
						if (s.mapMirror) u = -u;
						const double S = cap.width * pxPerCm;
						lp.ok = std::abs(u) < 0.5 && std::abs(v) < 0.5;
						lp.material = cap.material;
						lp.width = lp.height = S;
						lp.pivotX = u + 0.5;
						lp.pivotY = v + 0.5;
						lp.x = cx - lp.pivotX * S;
						lp.y = cy - lp.pivotY * S;
						lp.angle = -up - 90.0 * (s.mapQuarterTurns & 3);
					}
				} else if (s.mapImage == 1) {
					lp = localmap::Place(cx, cy, pxPerCm, up);
				}
				const double mirror = s.mapMirror && s.alwaysDrawLocalMap ? -1.0 : 1.0;
				// re-set only what changed (each call is a ProcessEvent into Slate)
				static double lastMirror = 0, lastX = -1e9, lastY = -1e9, lastW = -1, lastPx = -1, lastPy = -1, lastAngle = -1e9;
				if (lp.ok && mirror != lastMirror) {
					Vec2(mapImg, L"SetRenderScale", mirror, 1.0);
					lastMirror = mirror;
				}
				if (lp.ok) {
					if (lp.material != g_mapShownMaterial) {
						BrushFromMaterial(mapImg, lp.material);
						g_mapShownMaterial = lp.material;
					}
					if (auto* ms = g_mapSlot.Get()) {
						if (std::abs(lp.x - lastX) > 0.25 || std::abs(lp.y - lastY) > 0.25) {
							Vec2(ms, L"SetPosition", lp.x, lp.y);
							lastX = lp.x, lastY = lp.y;
						}
						if (std::abs(lp.width - lastW) > 0.25) {
							Vec2(ms, L"SetSize", lp.width, lp.height);
							lastW = lp.width;
						}
					}
					if (std::abs(lp.pivotX - lastPx) > 1e-4 || std::abs(lp.pivotY - lastPy) > 1e-4) {
						Vec2(mapImg, L"SetRenderTransformPivot", lp.pivotX, lp.pivotY);
						lastPx = lp.pivotX, lastPy = lp.pivotY;
					}
					if (std::abs(lp.angle - lastAngle) > 0.05) {
						Float(mapImg, L"SetRenderTransformAngle", static_cast<float>(lp.angle));
						lastAngle = lp.angle;
					}
				}
				if (lp.ok != g_mapImgShown) {
					SetVisible(mapImg, lp.ok);
					g_mapImgShown = lp.ok;
					logger::info("minimap: {}", lp.ok ? "showing the game's local map" : "the game's local map does not cover you here - parchment");
				}
			}

			std::vector<RawMarker> raw;
			if (vm && g_ml.ok) {
				auto* base = reinterpret_cast<std::uint8_t*>(vm);
				const auto* data = *reinterpret_cast<std::uint8_t* const*>(base + g_ml.arr);
				const auto  num = *reinterpret_cast<const std::int32_t*>(base + g_ml.arr + 8);
				if (data && num > 0 && num < 4096) {
					for (std::int32_t i = 0; i < num; ++i) {
						const auto* e = data + static_cast<std::size_t>(i) * static_cast<std::size_t>(g_ml.elem);
						RawMarker m{};
						m.angle = *reinterpret_cast<const float*>(e + g_ml.angle);
						m.distance = *reinterpret_cast<const float*>(e + g_ml.distance);
						m.type = g_ml.type >= 0 ? e[g_ml.type] : 0;
						m.visible = g_ml.visible < 0 || e[g_ml.visible] != 0;
						m.door = g_ml.door >= 0 && e[g_ml.door] != 0;
						m.quest = g_ml.quest >= 0 && e[g_ml.quest] != 0;
						m.player = g_ml.player >= 0 && e[g_ml.player] != 0;
						raw.push_back(m);
					}
				}
			}
			std::vector<std::array<float, 2>> hostiles;   // the compass' own (distance and angle)
			if (vm && g_ml.hostileArr >= 0 && g_ml.hostileElem > 0 && g_ml.hAngle >= 0 && g_ml.hDistance >= 0 && s.markHostiles) {
				auto* base = reinterpret_cast<std::uint8_t*>(vm);
				const auto* data = *reinterpret_cast<std::uint8_t* const*>(base + g_ml.hostileArr);
				const auto  num = *reinterpret_cast<const std::int32_t*>(base + g_ml.hostileArr + 8);
				if (data && num > 0 && num < 1024) {
					for (std::int32_t i = 0; i < num; ++i) {
						const auto* e = data + static_cast<std::size_t>(i) * static_cast<std::size_t>(g_ml.hostileElem);
						hostiles.push_back({ *reinterpret_cast<const float*>(e + g_ml.hAngle), *reinterpret_cast<const float*>(e + g_ml.hDistance) });
					}
				}
			}

			// a direction (dx, dy: a unit vector in panel units, y down) and a distance in panel units -> panel units; false
			// when outside the frame (a_clampToRim keeps it on the edge, pointing the way - DEM's quest pointer)
			const auto place = [&](double dx, double dy, double r, double a_iconHalf, bool a_clampToRim, double& a_x, double& a_y) {
				const double rim = half - a_iconHalf;
				double       reach = rim;   // the distance to the frame's edge along this direction
				if (!circle) {
					const double m = std::max(std::abs(dx), std::abs(dy));
					reach = m > 1e-6 ? rim / m : rim;
				}
				if (r > reach) {
					if (!a_clampToRim) return false;
					r = reach;
				}
				a_x = cx + dx * r;
				a_y = cy + dy * r;
				return true;
			};
			// a point at compass bearing a_angle (degrees) and a_distance from the player
			const auto plot = [&](float a_angle, float a_distance, double a_iconHalf, bool a_clampToRim, double& a_x, double& a_y) {
				const double t = (a_angle - up) * std::numbers::pi / 180.0;
				return place(std::sin(t), -std::cos(t), a_distance * pxPerCm, a_iconHalf, a_clampToRim, a_x, a_y);
			};
			// a point at an Unreal world position, in the map image's own frame: east +X, south +Y, the same quarter turns
			// and mirror as the image, turned by the image's angle
			std::array<double, 3> me{};
			bool haveMe = false;
			{
				static ue::Getter getPawn(L"K2_GetPawn");
				static ue::Getter location(L"K2_GetActorLocation");
				UE::UObject* pawn = nullptr;
				auto* pc = ue::PlayerController();
				haveMe = pc && getPawn.Get(pc, pawn) && pawn && ue::IsLive(pawn) && location.Get(pawn, me);
			}
			const double imageAngle = (-up - 90.0 * (s.mapQuarterTurns & 3)) * std::numbers::pi / 180.0;
			const auto plotWorld = [&](const std::array<double, 3>& a_at, double a_iconHalf, double& a_x, double& a_y) {
				double u = a_at[0] - me[0], v = a_at[1] - me[1];
				for (int k = 0; k < (s.mapQuarterTurns & 3); ++k) {
					const double t = u;
					u = -v, v = t;
				}
				if (s.mapMirror && s.alwaysDrawLocalMap) u = -u;
				const double sx = u * std::cos(imageAngle) - v * std::sin(imageAngle);
				const double sy = u * std::sin(imageAngle) + v * std::cos(imageAngle);
				const double len = std::hypot(sx, sy);
				if (len < 1e-3) {
					a_x = cx, a_y = cy;
					return true;
				}
				return place(sx / len, sy / len, len * pxPerCm, a_iconHalf, false, a_x, a_y);
			};

			int used = 0;
			const double icon = 22.0 * s.iconScale;
			for (const auto& m : raw) {
				if (used >= kPool) break;
				if (!m.visible) continue;
				int kind = -1;
				double sz = icon;
				bool   rim = false;
				bool   dim = false;
				if (m.quest || m.player) {
					if (!s.markQuestTargets) continue;
					kind = kKindQuest, sz = 18.0 * s.iconScale, rim = true;
				} else if (m.door || m.type == 12) {
					if (!s.markDoors) continue;
					kind = 12;
				} else if (m.type > 0 && m.type < 12) {
					if (!s.markLocations) continue;
					kind = m.type;
					rim = s.farLocationsOnRim;
				} else {
					continue;
				}
				double x = 0, y = 0;
				if (!plot(m.angle, m.distance, sz * 0.5, rim, x, y)) continue;
				if (kind != kKindQuest && m.distance * pxPerCm > half - sz * 0.5) {
					dim = true;   // held on the rim: it is farther than the map reaches
					sz *= 0.8;
				}
				Place(g_icons[static_cast<std::size_t>(used++)], kind, x, y, sz, dim);
			}
			// enemies: from their own positions while the compass lists any; the compass' angles only when no body is found
			const ULONGLONG nowMs = GetTickCount64();
			if (hostiles.empty()) {
				g_hostilePawns.clear();
			} else if (nowMs >= g_nextHostileScan) {
				g_nextHostileScan = nowMs + 500;
				ScanHostilePawns();
			}
			const double hsz = 10.0 * s.iconScale;
			int fromWorld = 0;
			if (haveMe) {
				static ue::Getter hostileLocation(L"K2_GetActorLocation");
				for (const auto& hp : g_hostilePawns) {
					if (used >= kPool) break;
					auto* p = hp.Get();
					std::array<double, 3> at{};
					if (!p || !hostileLocation.Get(p, at)) continue;
					++fromWorld;
					double x = 0, y = 0;
					if (!plotWorld(at, hsz * 0.5, x, y)) continue;
					Place(g_icons[static_cast<std::size_t>(used++)], kKindHostile, x, y, hsz);
				}
			}
			if (fromWorld == 0) {
				for (const auto& hd : hostiles) {
					if (used >= kPool) break;
					double x = 0, y = 0;
					if (!plot(hd[0], hd[1], hsz * 0.5, false, x, y)) continue;
					Place(g_icons[static_cast<std::size_t>(used++)], kKindHostile, x, y, hsz);
				}
			}
			g_hostileSource = hostiles.empty() ? "none" : fromWorld > 0 ? std::format("{} from their positions", fromWorld) : "the compass' angles";
			for (int i = used; i < kPool; ++i) {
				auto& sl = g_icons[static_cast<std::size_t>(i)];
				if (sl.shown) {
					SetVisible(sl.image.Get(), false);
					sl.shown = false;
				}
			}
			g_drawn = used;
			g_listed = static_cast<int>(raw.size());
			g_hostiles = static_cast<int>(hostiles.size());
			g_sample.assign(raw.begin(), raw.begin() + std::min<std::size_t>(raw.size(), 6));

			// the player arrow: at the player's place (it moves with a pan); north-up turns it to the heading
			if (auto* arrow = g_arrow.Get()) {
				const double sz = 26.0 * s.iconScale;
				static double lastSize = -1, lastPanX = -1e9, lastPanY = -1e9, lastAngle = -1e9;
				if (auto* as = g_arrowSlot.Get()) {
					if (sz != lastSize) {
						Vec2(as, L"SetSize", sz, sz);
						lastSize = sz;
					}
					if (g_panX != lastPanX || g_panY != lastPanY) {
						Vec2(as, L"SetPosition", g_panX, g_panY);
						lastPanX = g_panX, lastPanY = g_panY;
					}
				}
				const double angle = s.followCameraRotation ? 0.0 : g_heading;
				if (std::abs(angle - lastAngle) > 0.05) {
					Float(arrow, L"SetRenderTransformAngle", static_cast<float>(angle));
					lastAngle = angle;
				}
			}
		}
	}

	void Tick()
	{
		const auto now = Clock::now();
		const double dt = g_last.time_since_epoch().count() == 0 ? 0.0 : std::min(0.1, std::chrono::duration<double>(now - g_last).count());
		g_last = now;
		const auto& s = settings::Get();

		std::vector<Action> todo;
		{
			std::scoped_lock l(g_queueLock);
			todo.swap(g_queue);
		}

		auto*      im = RE::InterfaceManager::GetInstance(false, false);
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const bool gameplay = im && player && im->menuMode == 1;   // Oblivion Remastered: 1 is gameplay (logic library 7859)
		const bool built = g_root.Get() && g_panel.Get() && g_panelSlot.Get() && g_bg.Get();

		if (!s.enabled) {
			if (built && g_rootShown) {
				SetVisible(g_root.Get(), false);
				g_rootShown = false;
			}
			g_ownsPopup.store(false);
			g_ownsCompass.store(false);
			popup::Tick(popup::Widget::kCompass, g_rect, false, 0.0, false, 1.0);
			popup::Tick(popup::Widget::kBanner, g_rect, false, 0.0, false, 1.0);
			controls::Tick(false, false, dt);
			Status("off (bEnabled = 0)");
			return;
		}
		if (!built) {
			g_ownsPopup.store(false);
			controls::Tick(false, false, dt);
			if (!player || !ue::SelfCheck() || !Build()) return;
		}

		for (const auto a : todo) {
			switch (a) {
			case Action::kToggleShown: g_shownRuntime = !g_shownRuntime; break;
			case Action::kToggleZoom: g_zoomIn = !g_zoomIn; break;
			case Action::kRecentre: g_panX = g_panY = 0.0; break;
			case Action::kRebuild: g_root = {}; return;
			}
		}

		localmap::Tick(!gameplay);   // while a menu is open: read the game's map page if it holds a local map
		controls::Tick(gameplay, g_shownRuntime, dt);
		if (controls::TakeToggleShown()) {
			g_shownRuntime = !g_shownRuntime;   // a runtime toggle: bShowOnGameStart is not written (DEM)
			logger::info("minimap: {} by the hide key", g_shownRuntime ? "shown" : "hidden");
		}
		if (controls::TakeToggleZoom()) {
			g_zoomIn = !g_zoomIn;
			logger::info("minimap: zoom {}", g_zoomIn ? "in" : "out");
		}
		if (controls::Panning()) {
			const auto d = controls::TakePan();
			const double lim = g_layout.size;
			g_panX = std::clamp(g_panX - d[0], -lim, lim);   // moving the mouse right pulls the map right: the view goes left
			g_panY = std::clamp(g_panY - d[1], -lim, lim);
		} else if (g_panX != 0.0 || g_panY != 0.0) {
			controls::TakePan();
			g_panX = g_panY = 0.0;   // released: the map recentres (DEM)
		}

		const bool visible = gameplay && g_shownRuntime;
		capture::Tick(visible);   // the always-drawn local map: only while the minimap is on screen in gameplay
		if (visible != g_rootShown) {
			SetVisible(g_root.Get(), visible);
			g_rootShown = visible;
			logger::debug("minimap: {}", visible ? "on screen" : (gameplay ? "hidden by the player" : "hidden while a menu is open"));
		}

		const ULONGLONG ms = GetTickCount64();
		if (ms >= g_nextLayout) {
			g_nextLayout = ms + 500;
			Layout();
		}
		if (visible && ms >= g_nextMarkers) {
			g_nextMarkers = ms + 50;   // 20 updates a second; each only re-sets what moved
			Markers(dt);
		}
		// the banner follows the minimap only while the minimap is on screen; otherwise HUD Position Manager's layout (or
		// the game's) applies - and the export says so (the primary agent's request, 2026-09-29)
		const bool link = s.linkLocationPopup && g_rect.valid && visible;
		const double gap = s.popupGap / std::max(0.1, g_dpi);
		const bool   pair = s.pairCompass && g_rect.valid && visible;
		const auto   compassRect = popup::Tick(popup::Widget::kCompass, g_rect, pair, gap, s.fitCompassToMinimap, s.compassScale);
		// the banner after the compass when the compass is paired and placed, else after the minimap
		popup::Tick(popup::Widget::kBanner, compassRect.valid ? compassRect : g_rect, link, gap, s.fitPopupToMinimap, s.popupScale);
		g_ownsPopup.store(link && popup::Placing(popup::Widget::kBanner));
		g_ownsCompass.store(pair && popup::Placing(popup::Widget::kCompass));

		// the status copy for the page and the tool: four times a second, never every frame
		static ULONGLONG nextState = 0;
		if (ms < nextState) return;
		nextState = ms + 250;
		Status(visible ? "on screen" : (gameplay ? "hidden (the hide key)" : "waiting (a menu is open)"));
		std::scoped_lock l(g_stateLock);
		json sample = json::array();
		for (const auto& m : g_sample) {
			sample.push_back({ { "angle", m.angle }, { "distance", m.distance }, { "type", m.type }, { "visible", m.visible }, { "door", m.door },
				{ "quest", m.quest }, { "player", m.player } });
		}
		g_state = { { "built", g_builds }, { "gameplay", gameplay }, { "shown_runtime", g_shownRuntime }, { "on_screen", g_rootShown },
			{ "rect", { g_rect.x, g_rect.y, g_rect.w, g_rect.h } }, { "viewport_units", { g_viewportW, g_viewportH } }, { "dpi", g_dpi },
			{ "zoomed_in", g_zoomIn }, { "pan", { g_panX, g_panY } }, { "heading", g_heading },
			{ "local_map", localmap::State() }, { "local_map_shown", g_mapImgShown }, { "capture", capture::State() },
			{ "markers", { { "listed", g_listed }, { "drawn", g_drawn }, { "hostiles", g_hostiles }, { "hostiles_from", g_hostileSource }, { "layout_ok", g_ml.ok }, { "sample", sample } } } };
	}

	void Queue(Action a_action)
	{
		std::scoped_lock l(g_queueLock);
		g_queue.push_back(a_action);
	}

	bool OwnsLocationPopup() { return g_ownsPopup.load(); }
	bool OwnsCompass() { return g_ownsCompass.load(); }

	json State()
	{
		std::scoped_lock l(g_stateLock);
		json j = g_state;
		j["status"] = g_status;
		return j;
	}
}
