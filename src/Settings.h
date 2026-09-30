#pragma once

// Minimap Menu's settings. ONE table (Settings.cpp, kTable - one row per line) names every INI key, its section, its
// default and its range; Load, Save, the page and minimap.drive all go through it. tools/gen.py generates the shipped
// INI from the same rows, and at start the plugin checks that Values{} holds exactly the table's defaults and that no two
// rows share memory - so the compiled defaults and the shipped file cannot drift (rule 16) and a row can never point
// at the wrong member (CCM's offsetof lesson, logic library 2026-09-29).
//
// Indexed members are C arrays, never std::array: offsetof through std::array::operator[] resolved to the wrong
// offsets under MSVC (the same logic library entry).

namespace settings
{
	enum class Anchor : std::int32_t { kTopLeft = 0, kTopRight = 1, kBottomLeft = 2, kBottomRight = 3 };

	struct Values
	{
		// [General]
		bool enabled = true;
		bool showOnGameStart = true;   // written only by a deliberate page choice - a key press is a runtime toggle (DEM)

		// [Display] - Dragon's Eye Minimap's positioning (plan section 4)
		std::int32_t anchor = 1;       // top right, DEM's default
		float        offsetX[4]{};     // one pair per corner, viewport pixels; +x always right, +y always down
		float        offsetY[4]{};
		float        scale = 0.5f;     // capped at a quarter of the screen
		std::int32_t shape = 0;        // 0 square, 1 circle
		float        opacity = 1.0f;
		bool         linkLocationPopup = true;   // the game's area banner follows the minimap's corner (plan section 5)
		float        popupGap = 8.0f;
		bool         fitPopupToMinimap = true;   // the banner's text scaled down to the minimap's width
		float        popupScale = 1.0f;
		bool         pairCompass = false;        // the compass under (or over) the minimap, where the banner was; the banner after it
		bool         fitCompassToMinimap = true;
		float        compassScale = 1.0f;

		// [Map]
		std::int32_t mapImage = 1;     // 0 parchment only, 1 the game's own local map where it covers the player (else parchment)
		float        radiusMetres = 60.0f;
		float        radiusInteriorMetres = 25.0f;   // interiors are smaller: a reach of their own
		bool         followCameraRotation = true;
		bool         alwaysDrawLocalMap = true;    // the owner: draw the local map around the player at all times (Capture.h)

		// [Rendering] - the always-drawn local map
		float        captureWidthMetres = 180.0f;  // the area drawn around the player (the owner: "local to the area the player inhabits")
		float        interiorCutMetres = 2.5f;     // indoors, the camera's height above the feet: ceilings and upper floors stay out
		float        recaptureMoveFraction = 0.25f;   // a new capture after moving this share of the width
		std::int32_t mapQuarterTurns = 0;          // a correction if the drawn map comes out turned (0-3 quarter turns clockwise)
		bool         mapMirror = false;            // a correction if it comes out mirrored
		bool         skipWhileWorldSettles = true; // DEM: no redraw during a load and for iSettleMs after it
		std::int32_t settleMs = 1500;
		std::int32_t redrawIntervalMs = 1000;      // the least time between two of the minimap's own captures

		// [Controls] - DEM's control logic (plan section 6a); keys from .MD\DEFAULT-KEYS.md
		std::int32_t hideKey = 0x25;        // K: tap = hide / show, hold = pan (while holdHideToPan)
		std::int32_t zoomToggleKey = 0x26;  // L: tap = zoom in / out
		bool         holdHideToPan = true;  // the owner's switch (in DEM hold-to-pan was always on)
		float        holdToPanSecs = 0.25f;
		float        zoomDefault = 1.0f;    // how much the map is magnified at each of the two zoom levels
		float        zoomZoomedIn = 2.0f;
		bool         gamepadHideButton = true;          // the owner: R3 by default
		std::int32_t panHoldGamepadButton = 0x0080;     // XInput mask: 0x0080 right stick click, 0x0040 left stick click
		float        panSpeed = 1.0f;

		// [Markers]
		bool  markDoors = true;
		bool  markLocations = true;
		bool  markQuestTargets = true;
		bool  markHostiles = false;
		bool  farLocationsOnRim = true;   // a location beyond the reach stays on the rim, dimmed, pointing the way
		float iconScale = 1.0f;

		// [Log]
		std::int32_t logLevel = 2;   // info (rule 14, amended 2026-09-26)
	};

	// One row of the table: where a value lives in the INI and in Values.
	struct Field
	{
		const char* section;
		const char* key;
		enum class Kind { kBool, kInt, kFloat } kind;
		std::size_t offset;   // into Values
		double      def;      // the compiled default; Load() checks Values{} agrees with it at start (rule 16)
		double      min;
		double      max;
		const char* comment;  // the line written above the key in the shipped INI
	};

	const std::vector<Field>& Table();

	Values& Get();                     // game thread and page both read it; writes go through Set / Load
	Values  Defaults();
	std::filesystem::path IniPath();   // <game>\Binaries\Win64\OBSE\Plugins\MinimapMenu.ini
	std::filesystem::path PluginFolder();

	void Load();   // missing keys keep the compiled default and are logged at debug
	bool Save();   // rewrites only this mod's keys, in place; comments and unknown lines are kept

	// Generic access by "Section.Key" for minimap.drive: returns false (and a reason) for an unknown key or a value out
	// of range. The value is clamped to the table's range, never silently wrapped.
	bool SetByName(const std::string& a_name, const json& a_value, std::string& a_why);
	json GetAll();

	// The full INI text as the compiled defaults write it - what tools/gen.py and the gate compare against.
	std::string DefaultIniText();
}
