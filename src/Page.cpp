#include "Page.h"

#include <imgui.h>

#include "AMF.h"
#include "Controls.h"
#include "Minimap.h"
#include "Popup.h"
#include "Settings.h"
#include "Strings.h"

namespace page
{
	namespace
	{
		// An on/off switch (rule 32 - never a checkbox): the framework's own design (a red/green track and a white
		// knob in fixed colours, because AMF's theme leaves Button and FrameBg clear). A navigable item for the pad.
		bool Switch(const char* a_label, bool* a_v)
		{
			ImGui::PushID(a_label);
			const float h = ImGui::GetFrameHeight();
			const float w = h * 2.0f;
			const float rr = h * 0.5f;
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const bool pressed = ImGui::InvisibleButton("##switch", ImVec2(w, h));
			if (pressed) *a_v = !*a_v;
			const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			auto* dl = ImGui::GetWindowDrawList();
			const ImU32 track = *a_v ? (hovered ? IM_COL32(92, 191, 96, 255) : IM_COL32(76, 175, 80, 255))
			                         : (hovered ? IM_COL32(207, 84, 84, 255) : IM_COL32(191, 68, 68, 255));
			dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), track, rr);
			dl->AddCircleFilled(ImVec2(p.x + rr + (*a_v ? w - h : 0.0f), p.y + rr), rr - 2.0f, IM_COL32(240, 240, 240, 255), 32);
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_label);
			ImGui::PopID();
			return pressed;
		}

		void Hint(const char* a_text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", a_text);
			ImGui::PopStyleColor();
		}

		bool g_dirty = false;
		std::chrono::steady_clock::time_point g_dirtySince{};

		void Changed()
		{
			g_dirty = true;
			g_dirtySince = std::chrono::steady_clock::now();
		}

		// Sliders change every frame while dragged: the INI is written once they have been still for half a second.
		void SaveIfSettled()
		{
			if (g_dirty && std::chrono::steady_clock::now() - g_dirtySince > 500ms && !ImGui::IsAnyItemActive()) {
				g_dirty = false;
				settings::Save();
			}
		}

		bool Begin()
		{
			if (!AMF::UseFrameworkImGui()) return false;
			strings::Refresh();
			controls::NotePageDrawn();
			return true;
		}

		float Wide() { return ImGui::GetContentRegionAvail().x * 0.6f; }

		void DrawMap()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			if (Switch(TR("Enabled", "Minimap on"), &s.enabled)) Changed();
			if (Switch(TR("ShowOnStart", "Shown when a game loads"), &s.showOnGameStart)) Changed();
			Hint(TR("ShowOnStartHint", "The hide key only hides the minimap until the next load; this is the setting that stays."));

			ImGui::SeparatorText(TR("SectionPosition", "Position"));
			const char* corners[4] = { TR("TopLeft", "Top left"), TR("TopRight", "Top right"), TR("BottomLeft", "Bottom left"), TR("BottomRight", "Bottom right") };
			ImGui::SetNextItemWidth(Wide());
			if (ImGui::Combo(TR("Corner", "Corner"), &s.anchor, corners, 4)) Changed();
			const int c = std::clamp(s.anchor, 0, 3);
			if (ImGui::SliderFloat(TR("OffsetX", "Nudge right (this corner)"), &s.offsetX[c], -500.0f, 500.0f, "%.0f px")) Changed();
			if (ImGui::SliderFloat(TR("OffsetY", "Nudge down (this corner)"), &s.offsetY[c], -500.0f, 500.0f, "%.0f px")) Changed();
			Hint(TR("OffsetHint", "Each corner keeps its own nudge. 0 sits flush with the screen edges; right and down are always positive."));
			if (ImGui::SliderFloat(TR("Scale", "Size"), &s.scale, 0.1f, 1.5f, "%.2f")) Changed();
			Hint(TR("ScaleHint", "Capped at a quarter of the screen."));
			const char* shapes[2] = { TR("Square", "Square"), TR("Circle", "Circle") };
			ImGui::SetNextItemWidth(Wide());
			if (ImGui::Combo(TR("Shape", "Shape"), &s.shape, shapes, 2)) Changed();
			if (ImGui::SliderFloat(TR("Opacity", "Opacity"), &s.opacity, 0.1f, 1.0f, "%.2f")) Changed();

			ImGui::SeparatorText(TR("SectionPopup", "Location banner"));
			if (Switch(TR("LinkPopup", "The location banner follows the minimap"), &s.linkLocationPopup)) Changed();
			Hint(TR("LinkPopupHint", "The name that appears when you enter a place moves under the minimap at a top corner and above it at a bottom corner."));
			ImGui::BeginDisabled(!s.linkLocationPopup);
			if (ImGui::SliderFloat(TR("PopupGap", "Gap"), &s.popupGap, 0.0f, 100.0f, "%.0f px")) Changed();
			ImGui::EndDisabled();

			ImGui::SeparatorText(TR("SectionMap", "Map"));
			const char* images[2] = { TR("ImagePaper", "Parchment"), TR("ImageGame", "The game's local map (experimental)") };
			ImGui::SetNextItemWidth(Wide());
			if (ImGui::Combo(TR("Image", "Map picture"), &s.mapImage, images, 2)) Changed();
			if (ImGui::SliderFloat(TR("Radius", "Reach at normal zoom"), &s.radiusMetres, 10.0f, 500.0f, "%.0f m")) Changed();
			if (Switch(TR("FollowCamera", "Turn with the camera"), &s.followCameraRotation)) Changed();
			Hint(TR("FollowCameraHint", "On: up is where you look. Off: north is always up."));
			SaveIfSettled();
		}

		const char* RefusalText(int a_i)
		{
			switch (a_i) {
			case 0: return TR("ResFramework", "the menu framework uses it");
			case 1: return TR("ResTab", "Tab opens the game's quick keys");
			case 2: return TR("ResOtherAction", "another of this mod's actions uses it");
			case 3: return TR("ResOurMod", "another of our mods uses it by default");
			default: return TR("ResMouse", "mouse buttons cannot be bound");
			}
		}

		void KeyRow(const char* a_label, std::int32_t a_scan, int a_target, const char* a_id)
		{
			ImGui::PushID(a_id);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(a_label);
			ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
			const bool capturing = controls::Capturing() == a_target;
			if (capturing) {
				ImGui::TextUnformatted(TR("PressKey", "Press a key (Escape cancels)"));
				ImGui::SameLine();
				if (ImGui::Button(TR("Cancel", "Cancel"))) controls::CancelCapture();
			} else {
				ImGui::Text("%s", controls::KeyName(a_scan).c_str());
				ImGui::SameLine();
				if (ImGui::Button(TR("Bind", "Bind"))) controls::BeginCapture(a_target);
				ImGui::SameLine();
				if (ImGui::Button(TR("Unbind", "Unbind"))) {
					auto& v = settings::Get();
					(a_target == 1 ? v.hideKey : v.zoomToggleKey) = 0;
					Changed();
				}
			}
			ImGui::PopID();
		}

		void DrawControls()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			Hint(TR("ControlsHint", "The keys work in gameplay only, never while a menu is open."));
			KeyRow(TR("HideKey", "Hide / pan key"), s.hideKey, 1, "hide");
			KeyRow(TR("ZoomKey", "Zoom key"), s.zoomToggleKey, 2, "zoom");
			if (const int r = controls::LastRefusal(); r >= 0) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.35f, 1.0f));
				ImGui::TextWrapped("%s: %s", TR("Refused", "That key cannot be bound"), RefusalText(r));
				ImGui::PopStyleColor();
			}
			if (Switch(TR("HoldToPan", "Hold the hide key to pan the map"), &s.holdHideToPan)) Changed();
			Hint(TR("HoldToPanHint", "A tap hides or shows the minimap. Held, the mouse (or the stick) pans it until you let go; then it recentres."));
			ImGui::BeginDisabled(!s.holdHideToPan);
			if (ImGui::SliderFloat(TR("HoldSecs", "Hold for (seconds)"), &s.holdToPanSecs, 0.05f, 2.0f, "%.2f")) Changed();
			if (ImGui::SliderFloat(TR("PanSpeed", "Pan speed"), &s.panSpeed, 0.1f, 5.0f, "%.1f")) Changed();
			ImGui::EndDisabled();
			if (ImGui::SliderFloat(TR("ZoomNormal", "Normal zoom"), &s.zoomDefault, 0.25f, 8.0f, "%.2fx")) Changed();
			if (ImGui::SliderFloat(TR("ZoomIn", "Other zoom"), &s.zoomZoomedIn, 0.25f, 8.0f, "%.2fx")) Changed();

			ImGui::SeparatorText(TR("SectionController", "Controller"));
			if (Switch(TR("PadButton", "A controller button hides and pans too"), &s.gamepadHideButton)) Changed();
			ImGui::BeginDisabled(!s.gamepadHideButton);
			int pad = s.panHoldGamepadButton == 0x0040 ? 1 : 0;
			const char* pads[2] = { TR("PadR3", "Right stick click"), TR("PadL3", "Left stick click") };
			ImGui::SetNextItemWidth(Wide());
			if (ImGui::Combo(TR("PadWhich", "Button"), &pad, pads, 2)) {
				s.panHoldGamepadButton = pad == 1 ? 0x0040 : 0x0080;
				Changed();
			}
			ImGui::EndDisabled();
			SaveIfSettled();
		}

		void DrawMarkers()
		{
			if (!Begin()) return;
			auto& s = settings::Get();
			if (Switch(TR("MarkQuests", "Quest targets"), &s.markQuestTargets)) Changed();
			Hint(TR("MarkQuestsHint", "A quest target beyond the edge stays on the rim, pointing the way."));
			if (Switch(TR("MarkDoors", "Doors"), &s.markDoors)) Changed();
			if (Switch(TR("MarkLocations", "Locations"), &s.markLocations)) Changed();
			if (Switch(TR("MarkHostiles", "Enemies"), &s.markHostiles)) Changed();
			if (ImGui::SliderFloat(TR("IconScale", "Icon size"), &s.iconScale, 0.3f, 3.0f, "%.2f")) Changed();
			SaveIfSettled();
		}

		void DrawStatus()
		{
			if (!Begin()) return;
			Hint(TR("StatusHint", "Live values, read from the game as it runs."));
			const auto m = minimap::State();
			const auto p = popup::State();
			const auto c = controls::State();
			ImGui::TextWrapped("%s: %s", TR("RowMinimap", "Minimap"), m.value("status", std::string{}).c_str());
			ImGui::TextWrapped("%s: %s", TR("RowPopup", "Location banner"), p.value("status", std::string{}).c_str());
			ImGui::TextWrapped("%s", m.dump(1).c_str());
			ImGui::TextWrapped("%s", p.dump(1).c_str());
			ImGui::TextWrapped("%s", c.dump(1).c_str());
		}
	}

	void Register()
	{
		if (!AMF::IsInstalled()) {
			logger::info("the Apocrypha Menu Framework is not installed - no menu page; the minimap runs from its INI");
			return;
		}
		const bool a = AMF::RegisterPage(kModName, "Map", &DrawMap);
		const bool b = AMF::RegisterPage(kModName, "Controls", &DrawControls);
		const bool c = AMF::RegisterPage(kModName, "Markers", &DrawMarkers);
		const bool d = AMF::RegisterPage(kModName, "Status", &DrawStatus);
		logger::info("AMF {} (API {}): pages Map={}, Controls={}, Markers={}, Status={}", AMF::Version(), AMF::APIVersion(), a, b, c, d);
	}
}
