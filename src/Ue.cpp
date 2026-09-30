#include "Ue.h"

namespace ue
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kPropertyOffsetInternal = 0x44;

		std::atomic<int> g_state{ 0 };   // 0 unchecked, 1 proven, -1 failed

		std::string Narrow(const wchar_t* a_w)
		{
			std::string out;
			for (; a_w && *a_w; ++a_w) {
				out.push_back(*a_w < 0x80 ? static_cast<char>(*a_w) : '?');
			}
			return out;
		}
	}

	std::string Utf8(const UE::FString& a_s)
	{
		const wchar_t* d = UE::GetData(a_s);
		const int      n = UE::GetNum(a_s);
		if (!d || n <= 0) {
			return {};
		}
		const int len = d[n - 1] == L'\0' ? n - 1 : n;
		const int bytes = WideCharToMultiByte(CP_UTF8, 0, d, len, nullptr, 0, nullptr, nullptr);
		std::string out(bytes > 0 ? static_cast<std::size_t>(bytes) : 0, '\0');
		if (bytes > 0) {
			WideCharToMultiByte(CP_UTF8, 0, d, len, out.data(), bytes, nullptr, nullptr);
		}
		return out;
	}

	std::string NameOf(UE::UObject* a_o)
	{
		return a_o ? Utf8(a_o->GetFName().ToString()) : std::string("null");
	}

	std::int32_t Offset(UE::UStruct* a_struct, std::string_view a_name)
	{
		for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
			for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
				if (Utf8(reinterpret_cast<const UE::FName*>(f + kFieldName)->ToString()) == a_name) {
					return *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal);
				}
			}
		}
		return -1;
	}

	std::int32_t SizeOf(UE::UStruct* a_struct, std::string_view a_name)
	{
		for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
			for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
				if (Utf8(reinterpret_cast<const UE::FName*>(f + kFieldName)->ToString()) == a_name) {
					return *reinterpret_cast<const std::int32_t*>(f + 0x34);   // FProperty::ElementSize
				}
			}
		}
		return -1;
	}

	UE::UObject* Load(const wchar_t* a_path)
	{
		if (!a_path) return nullptr;
		if (auto* o = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path)) {
			return o;
		}
		static auto* lib = Class(L"/Script/Engine.KismetSystemLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo) return nullptr;
		Call mk(cdo, L"MakeSoftObjectPath");
		Call conv(cdo, L"Conv_SoftObjPathToSoftObjRef");
		Call load(cdo, L"LoadAsset_Blocking");
		void* path = mk.At("PathString");
		const auto pathSize = mk.Size("ReturnValue");
		const auto refSize = conv.Size("ReturnValue");
		if (!mk || !conv || !load || !path || pathSize <= 0 || refSize <= 0 || conv.Size("SoftObjectPath") != pathSize ||
			load.Size("Asset") != refSize) {
			static bool warned = false;
			if (!warned) {
				warned = true;
				logger::warn("ue: KismetSystemLibrary cannot load assets here (MakeSoftObjectPath {}, Conv_SoftObjPathToSoftObjRef {}, LoadAsset_Blocking {}) - only assets already in memory are used",
					static_cast<bool>(mk), static_cast<bool>(conv), static_cast<bool>(load));
			}
			return nullptr;
		}
		// the FString is built in place; it and the structs copied from it are never destroyed - a small, one-time leak
		// per asset, cheaper than guessing the layouts' destructors
		new (path) UE::FString(a_path);
		mk.Run();
		std::memcpy(conv.At("SoftObjectPath"), mk.At("ReturnValue"), static_cast<std::size_t>(pathSize));
		conv.Run();
		std::memcpy(load.At("Asset"), conv.At("ReturnValue"), static_cast<std::size_t>(refSize));
		load.Run();
		auto* got = load.Get<UE::UObject*>("ReturnValue");
		logger::debug("ue: loaded {} -> {}", Narrow(a_path), got ? NameOf(got) : std::string("nothing"));
		return got;
	}

	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
	{
		auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
		auto* st = reinterpret_cast<UE::UStruct*>(fn);
		auto* f = st ? reinterpret_cast<std::uint8_t*>(st->childProperties) : nullptr;
		const auto off = f ? *reinterpret_cast<const std::int32_t*>(f + kPropertyOffsetInternal) : -1;
		if (off < 0) {
			return false;
		}
		std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(st->propertiesSize, 0)) + a_size + 16, 0);
		std::memcpy(params.data() + off, a_bytes, a_size);
		a_obj->ProcessEvent(fn, params.data());
		return true;
	}

	UE::UObject* PlayerController()
	{
		static Handle    cached;
		static ULONGLONG lastScan = 0;
		if (auto* pc = cached.Get()) {
			return pc;
		}
		const ULONGLONG now = GetTickCount64();
		if (now - lastScan < 2000) {
			return nullptr;   // the object array scan is not cheap: at most every 2 s until found (rule 17)
		}
		lastScan = now;
		auto* found = FirstOf(Class(L"/Script/Engine.PlayerController"));
		cached.Set(found);
		if (found) logger::debug("ue: player controller {}", NameOf(found));
		return found;
	}

	bool SelfCheck()
	{
		if (g_state.load() != 0) {
			return g_state.load() > 0;
		}
		// the same proof Tween Menu and Improved Wheel Menu use: KeyIndex was read in game at 0xD0 (2026-09-26)
		auto* vm = Class(L"/Script/Altar.VQuickKeysMenuViewModel");
		if (!vm) {
			return false;   // not loaded yet: asked again later (rule 17)
		}
		const auto keyIndex = Offset(vm, "KeyIndex");
		if (keyIndex < 0) {
			// the class exists but its property chain is not linked yet (early in a launch - CCM round 1, 12:22:24, read -1
			// here 2 s in and latched a failure, so nothing in CCM ever ran): asked again later, never latched (rule 17)
			static bool noted = false;
			if (!noted) {
				noted = true;
				logger::debug("ue: VQuickKeysMenuViewModel has no KeyIndex yet - the property layout is checked again later");
			}
			return false;
		}
		g_state.store(keyIndex == 0xD0 ? 1 : -1);
		if (g_state.load() > 0) {
			logger::info("ue: property offsets proven (VQuickKeysMenuViewModel KeyIndex at 0x{:X})", keyIndex);
		} else {
			logger::error("ue: KeyIndex read at 0x{:X}, expected 0xD0 - the property layout is not UE5's; the minimap cannot be built", keyIndex);
		}
		return g_state.load() > 0;
	}

	bool IsLive(UE::UObject* a_o)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!a_o || !arr) {
			return false;
		}
		const std::int32_t idx = a_o->internalIndex;
		if (idx < 0 || idx >= arr->GetObjectArrayNum()) {
			return false;
		}
		auto* item = arr->IndexToObject(idx);
		return item && reinterpret_cast<UE::UObject*>(item->object) == a_o;
	}

	void Handle::Set(UE::UObject* a_live)
	{
		ptr = a_live;
		index = a_live ? a_live->internalIndex : -1;
	}

	UE::UObject* Handle::Get() const
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!ptr || !arr || index < 0 || index >= arr->GetObjectArrayNum()) {
			return nullptr;
		}
		auto* item = arr->IndexToObject(index);
		return item && reinterpret_cast<UE::UObject*>(item->object) == ptr ? ptr : nullptr;
	}

	std::vector<UE::UObject*> AllOf(UE::UClass* a_base)
	{
		std::vector<UE::UObject*> out;
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_base) return out;
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			auto* cls = o ? o->GetClass() : nullptr;
			if (cls && cls->IsChildOf(a_base) && (static_cast<std::int32_t>(o->objectFlags) & 0x30) == 0) {   // not CDO (0x10), not archetype (0x20)
				out.push_back(o);
			}
		}
		arr->UnlockInternalArray();
		return out;
	}

	UE::UObject* FirstOf(UE::UClass* a_base)
	{
		auto* arr = UE::FUObjectArray::GetSingleton();
		if (!arr || !a_base) {
			return nullptr;
		}
		UE::UObject* found = nullptr;
		arr->LockInternalArray();
		const std::int32_t n = arr->GetObjectArrayNum();
		for (std::int32_t i = 0; i < n && !found; ++i) {
			auto* item = arr->IndexToObject(i);
			auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
			auto* cls = o ? o->GetClass() : nullptr;
			if (cls && cls->IsChildOf(a_base) && o != cls->GetDefaultObject(false) && (static_cast<std::int32_t>(o->objectFlags) & 0x30) == 0) {
				found = o;
			}
		}
		arr->UnlockInternalArray();
		return found;
	}

	bool Getter::Resolve(UE::UObject* a_obj)
	{
		auto* cls = a_obj ? a_obj->GetClass() : nullptr;
		if (!cls) {
			return false;
		}
		if (cls == m_class) {
			return m_fn != nullptr;
		}
		m_class = cls;
		m_fn = a_obj->FindFunction(UE::FName(m_name, UE::EFindName::Find));
		m_ret = -1;
		if (m_fn) {
			auto* st = reinterpret_cast<UE::UStruct*>(m_fn);
			m_ret = Offset(st, "ReturnValue");
			m_params.assign(static_cast<std::size_t>(std::max(st->propertiesSize, 0)), 0);
			if (m_ret < 0 || m_params.size() < static_cast<std::size_t>(m_ret) + 4) {
				logger::warn("ue: {} on {} has no readable ReturnValue", Narrow(m_name), NameOf(cls));
				m_fn = nullptr;
			} else {
				m_params.resize(std::max<std::size_t>(m_params.size(), static_cast<std::size_t>(m_ret) + 32), 0);
				logger::debug("ue: {} on {} - ReturnValue at 0x{:X}, {} parameter bytes", Narrow(m_name), NameOf(cls), m_ret,
					st->propertiesSize);
			}
		} else {
			logger::warn("ue: {} has no function {}", NameOf(cls), Narrow(m_name));
		}
		return m_fn != nullptr;
	}
}
