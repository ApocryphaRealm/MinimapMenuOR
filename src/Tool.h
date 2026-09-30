#pragma once

// The TestBench driving tools (rules 31 and 64): minimap.status (read-only) and minimap.drive. They run on TestBench's
// listener thread, so they read the State() copies made on the game thread and queue actions - never a UObject.

namespace tool
{
	bool Register();   // false until TestBench is loaded; safe to call again
}
