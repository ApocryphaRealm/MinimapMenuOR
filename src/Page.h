#pragma once

// Minimap Menu's pages in the Apocrypha Menu Framework. Registered at kPostLoad when AMF is installed; without AMF the
// minimap runs from its INI. Page callbacks read and write settings::Values and read the State() copies - never a
// UObject (the page draws on the render thread).

namespace page
{
	inline constexpr const char* kModName = "Minimap Menu";
	void Register();
}
