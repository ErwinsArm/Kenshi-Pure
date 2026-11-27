// KenshiPure's address layer at run time (see Sites.h). Runs once, before any hook or game call.
// Patterns are checked in the exe FILE (mapped read-only), not in the loaded image: another mod may
// already have hooked a function in memory, and the file's SHA-256 is what identifies the build.
//   Known build (SHA-256 in sites.tsv): every row's pattern is checked at the address the table
//   records for that build; an operand read there (global target, field offset) must equal the
//   table's value. Mismatch -> the row is unresolved.
//   Unknown build: each pattern is searched in the file's .text; a row needs exactly one hit; the
//   hits must keep the order of their 1.0.68 addresses (longest ordered subset is kept) and the
//   shift of each hit must match a neighbouring hit's shift (within kShiftSlack). Features whose
//   name ends in "-write" stay off; vtable slots are not checked there yet, so vslot rows stay
//   unresolved; and KenshiPure.cpp installs no game hook in an unknown build at all.

#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <algorithm>

#include "Sites.h"
#include "Log.h"

extern "C" void* kp_slots[KP_SITE_COUNT];   // kp_thunks.asm: one slot per row, code rows start at a trap stub

namespace kp
{
    ptrdiff_t g_fieldOff[KP_SITE_COUNT];
}

namespace
{
    int g_build = -1;                    // index into kp::g_builds, -1 = unknown
    bool g_resolved = false;
    bool g_ok[KP_SITE_COUNT];
    void* g_addr[KP_SITE_COUNT];
    long long g_value[KP_SITE_COUNT];
    bool g_featureOn[KP_FEATURE_COUNT];
    unsigned char* g_base = NULL;        // the loaded exe: where resolved addresses point
    volatile LONG g_trapLogged[KP_SITE_COUNT];
    const long long kShiftSlack = 0x10000;

    // The exe file, mapped read-only.
    struct FileImage
    {
        const unsigned char* view;
        unsigned long long size;
        const IMAGE_SECTION_HEADER* sec;
        int nsec;
        unsigned long textRva, textEnd;  // .text, as RVAs backed by file bytes
    } g_f;

    const unsigned char* At(long long rva, long long len)
    {
        if (rva < 0 || len < 0) return NULL;
        for (int i = 0; i < g_f.nsec; ++i)
        {
            const IMAGE_SECTION_HEADER& s = g_f.sec[i];
            if (rva >= s.VirtualAddress && rva + len <= (long long)s.VirtualAddress + s.SizeOfRawData)
            {
                unsigned long long off = s.PointerToRawData + (rva - s.VirtualAddress);
                return off + len <= g_f.size ? g_f.view + off : NULL;
            }
        }
        return NULL;
    }

    bool MapExe(const wchar_t* path, HANDLE* file, HANDLE* map)
    {
        ZeroMemory(&g_f, sizeof(g_f));
        *file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (*file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz;
        GetFileSizeEx(*file, &sz);
        *map = CreateFileMappingW(*file, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!*map) return false;
        g_f.view = static_cast<const unsigned char*>(MapViewOfFile(*map, FILE_MAP_READ, 0, 0, 0));
        if (!g_f.view) return false;
        g_f.size = sz.QuadPart;
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_f.view);
        if (g_f.size < 0x400 || dos->e_magic != IMAGE_DOS_SIGNATURE || (unsigned long long)dos->e_lfanew + 0x200 > g_f.size) return false;
        const IMAGE_NT_HEADERS64* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_f.view + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
        g_f.sec = IMAGE_FIRST_SECTION(nt);
        g_f.nsec = nt->FileHeader.NumberOfSections;
        for (int i = 0; i < g_f.nsec; ++i)
            if (memcmp(g_f.sec[i].Name, ".text\0\0\0", 8) == 0)
            {
                g_f.textRva = g_f.sec[i].VirtualAddress;
                g_f.textEnd = g_f.sec[i].VirtualAddress + min(g_f.sec[i].Misc.VirtualSize, g_f.sec[i].SizeOfRawData);
            }
        return g_f.textEnd > g_f.textRva;
    }

    bool InText(long long rva, int len) { return rva >= g_f.textRva && rva + len <= g_f.textEnd; }

    bool MatchAt(const kp::KpSiteRow& r, long long start)
    {
        if (!InText(start, r.patLen)) return false;
        const unsigned char* p = At(start, r.patLen);
        if (!p) return false;
        for (int i = 0; i < r.patLen; ++i)
            if (r.mask[i] && p[i] != r.pat[i]) return false;
        return true;
    }

    // Value a row stands for, read at its anchor (the site inside the matched pattern).
    long long ReadValue(const kp::KpSiteRow& r, long long anchor)
    {
        const unsigned char* a = At(anchor, 16);
        if (!a) return -1;
        switch (r.read)
        {
        case kp::RD_RIP: return anchor + r.readB + *reinterpret_cast<const int*>(a + r.readA);
        case kp::RD_DISP: return r.readB == 1 ? (long long)*reinterpret_cast<const signed char*>(a + r.readA)
                                              : (long long)*reinterpret_cast<const int*>(a + r.readA);
        default: return r.kind == kp::SK_FIELD ? 0 : anchor;   // field without displacement = offset 0
        }
    }

    // Target of a vtable slot, following one `jmp rel32` (Kenshi is linked incrementally).
    long long SlotTarget(long long vtable, long long slot, unsigned long long imageBase)
    {
        const unsigned char* p = At(vtable + slot, 8);
        if (!p) return -1;
        long long rva = (long long)(*reinterpret_cast<const unsigned long long*>(p) - imageBase);
        const unsigned char* f = At(rva, 5);
        if (!f || !InText(rva, 5)) return -1;
        if (f[0] == 0xE9) rva = rva + 5 + *reinterpret_cast<const int*>(f + 1);
        return rva;
    }

    unsigned long long FileImageBase()
    {
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_f.view);
        return reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_f.view + dos->e_lfanew)->OptionalHeader.ImageBase;
    }

    void Accept(int i, long long value)
    {
        const kp::KpSiteRow& r = kp::g_sites[i];
        g_ok[i] = true;
        g_value[i] = value;
        if (r.kind == kp::SK_FIELD) kp::g_fieldOff[i] = (ptrdiff_t)value;
        else if (r.kind != kp::SK_VSLOT) g_addr[i] = g_base + value;
        if (r.kind == kp::SK_CODE)
        {
            kp_slots[i] = g_addr[i];
            // Informative: another module already put a jump at the start of this function.
            const unsigned char* file = At(value, 1);
            const IMAGE_NT_HEADERS64* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                g_base + reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
            if (file && value < nt->OptionalHeader.SizeOfImage && g_base[value] != *file && (g_base[value] == 0xE9 || g_base[value] == 0xFF || g_base[value] == 0x48))
                kp::Log("sites: %s is already hooked in memory by another module (first byte %02x)", r.name, g_base[value]);
        }
    }

    // Known build: check each row where the table says it is.
    void ResolveKnown()
    {
        unsigned long long imageBase = FileImageBase();
        for (int i = 0; i < KP_SITE_COUNT; ++i)
        {
            const kp::KpSiteRow& r = kp::g_sites[i];
            const kp::KpBuildValue& v = r.builds[g_build];
            if (!v.known) continue;
            if (!r.pat) { Accept(i, v.value); continue; }   // header-only row (no anchor in code)
            if (r.kind == kp::SK_VSLOT)
            {
                long long f = SlotTarget(v.anchor, v.value, imageBase);
                if (f >= 0 && MatchAt(r, f - r.site)) Accept(i, v.value);
                continue;
            }
            if (!MatchAt(r, v.anchor - r.site)) continue;
            if (ReadValue(r, v.anchor) != v.value) continue;
            Accept(i, v.value);
        }
    }

    void LongestRun(const kp::KpSiteRow& r, int* start, int* len)
    {
        *start = 0; *len = 0;
        int cur = 0;
        for (int i = 0; i < r.patLen; ++i)
        {
            cur = r.mask[i] ? cur + 1 : 0;
            if (cur > *len) { *len = cur; *start = i - cur + 1; }
        }
    }

    // Number of hits of a row's pattern in the file's .text (stops at 2); *hit = the first (RVA).
    int Scan(const kp::KpSiteRow& r, long long* hit)
    {
        int rs, rl;
        LongestRun(r, &rs, &rl);
        if (rl == 0) return 0;
        const unsigned char* text = At(g_f.textRva, g_f.textEnd - g_f.textRva);
        if (!text) return 0;
        int n = 0;
        const unsigned char* p = text + rs;
        const unsigned char* end = text + (g_f.textEnd - g_f.textRva) - (r.patLen - rs);
        while (p < end)
        {
            p = static_cast<const unsigned char*>(memchr(p, r.pat[rs], end - p));
            if (!p) break;
            long long rva = g_f.textRva + (p - text) - rs;
            if (memcmp(p, r.pat + rs, rl) == 0 && MatchAt(r, rva))
            {
                *hit = rva;
                if (++n >= 2) break;
            }
            ++p;
        }
        return n;
    }

    struct Hit { long long ref; long long at; int row; };
    bool ByRef(const Hit& a, const Hit& b) { return a.ref < b.ref; }

    // Unknown build: search every pattern; keep unique hits that keep the 1.0.68 order and whose
    // shift agrees with a neighbouring hit's.
    void ResolveUnknown()
    {
        const int ref = KP_BUILD_COUNT - 1;   // newest measured build is the reference
        std::vector<Hit> hits;
        for (int i = 0; i < KP_SITE_COUNT; ++i)
        {
            const kp::KpSiteRow& r = kp::g_sites[i];
            if (!r.pat || r.kind == kp::SK_VSLOT || !r.builds[ref].known) continue;
            long long at = 0;
            if (Scan(r, &at) != 1) continue;
            Hit h = { r.builds[ref].anchor, at + r.site, i };
            hits.push_back(h);
        }
        std::sort(hits.begin(), hits.end(), ByRef);
        size_t n = hits.size();
        std::vector<int> len(n, 1), prev(n, -1);
        int best = -1;
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = 0; j < i; ++j)
                if (hits[j].at < hits[i].at && len[j] + 1 > len[i]) { len[i] = len[j] + 1; prev[i] = (int)j; }
            if (best < 0 || len[i] > len[best]) best = (int)i;
        }
        std::vector<Hit> kept;
        for (int k = best; k >= 0; k = prev[k]) kept.push_back(hits[k]);
        std::reverse(kept.begin(), kept.end());
        for (size_t k = 0; k < kept.size(); ++k)
        {
            long long s = kept[k].at - kept[k].ref;
            bool fits = false;
            if (k > 0) fits = fits || _abs64(s - (kept[k - 1].at - kept[k - 1].ref)) <= kShiftSlack;
            if (k + 1 < kept.size()) fits = fits || _abs64(s - (kept[k + 1].at - kept[k + 1].ref)) <= kShiftSlack;
            if (!fits && kept.size() > 1)
            {
                kp::Log("sites: %s dropped (shift 0x%llx fits no neighbour)", kp::g_sites[kept[k].row].name, s);
                continue;
            }
            Accept(kept[k].row, ReadValue(kp::g_sites[kept[k].row], kept[k].at));
        }
        if (kept.size() < n) kp::Log("sites: %u of %u unique hits dropped (out of order)", (unsigned)(n - kept.size()), (unsigned)n);
    }
}

// Trap stub target (kp_thunks.asm): a game function was called whose row is not resolved.
extern "C" void KpTrapHit(int row)
{
    if (row >= 0 && row < KP_SITE_COUNT && InterlockedExchange(&g_trapLogged[row], 1) == 0)
        kp::Log("trap: %s was called but is not resolved for this build - call refused", kp::g_sites[row].name);
    RaiseException(0xE04B5001, EXCEPTION_NONCONTINUABLE, 0, NULL);   // the caller's __try turns it into an error reply
}

namespace kp
{
    bool SitesResolve(const wchar_t* exePath, const char* exeSha256)
    {
        for (int i = 0; i < KP_SITE_COUNT; ++i) { g_ok[i] = false; g_addr[i] = NULL; g_value[i] = 0; g_fieldOff[i] = kFieldTrap; }
        g_base = reinterpret_cast<unsigned char*>(GetModuleHandleW(NULL));
        g_build = -1;
        for (int b = 0; b < KP_BUILD_COUNT; ++b)
            if (exeSha256 && strcmp(exeSha256, g_builds[b].sha256) == 0) g_build = b;

        HANDLE file = INVALID_HANDLE_VALUE, map = NULL;
        ULONGLONG t0 = GetTickCount64();
        bool mapped = exePath && MapExe(exePath, &file, &map);
        if (!mapped) Log("sites: cannot read the exe file %ls - nothing resolved", exePath ? exePath : L"(none)");
        else if (g_build >= 0) ResolveKnown();
        else ResolveUnknown();
        ULONGLONG ms = GetTickCount64() - t0;
        if (g_f.view) UnmapViewOfFile(g_f.view);
        if (map) CloseHandle(map);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        ZeroMemory(&g_f, sizeof(g_f));

        int ok = 0;
        for (int i = 0; i < KP_SITE_COUNT; ++i) if (g_ok[i]) ++ok;
        Log("sites: build %s (%s); %d/%d rows resolved in %llu ms", g_build >= 0 ? g_builds[g_build].label : "unknown",
            g_build >= 0 ? g_builds[g_build].note : "not in sites.tsv: pattern search, read-only features only", ok,
            KP_SITE_COUNT, ms);

        for (int f = 0; f < KP_FEATURE_COUNT; ++f) g_featureOn[f] = true;
        for (int i = 0; i < KP_SITE_COUNT; ++i)
            if (!g_ok[i])
            {
                g_featureOn[g_sites[i].feature] = false;
                Log("sites: unresolved %s (%s)", g_sites[i].name, g_features[g_sites[i].feature]);
            }
        if (g_build < 0)
            for (int f = 0; f < KP_FEATURE_COUNT; ++f)
            {
                size_t n = strlen(g_features[f]);
                if (n >= 6 && strcmp(g_features[f] + n - 6, "-write") == 0) g_featureOn[f] = false;
            }
        if (!g_featureOn[0]) for (int f = 1; f < KP_FEATURE_COUNT; ++f) g_featureOn[f] = false;
        std::string list;
        for (int f = 0; f < KP_FEATURE_COUNT; ++f)
        {
            if (f) list += ", ";
            list += g_features[f];
            list += g_featureOn[f] ? " on" : " OFF";
        }
        Log("sites: features: %s", list.c_str());
        g_resolved = true;
        return g_featureOn[0];
    }

    const char* SitesBuildName() { return g_build >= 0 ? g_builds[g_build].label : "unknown"; }
    bool SitesKnownBuild() { return g_build >= 0; }
    bool SiteOk(int id) { return id >= 0 && id < KP_SITE_COUNT && g_ok[id]; }
    void* SiteAddress(int id) { return SiteOk(id) ? g_addr[id] : NULL; }
    long long SiteValue(int id) { return SiteOk(id) ? g_value[id] : -1; }

    bool FeatureOn(const char* feature)
    {
        if (!g_resolved) return false;
        for (int f = 0; f < KP_FEATURE_COUNT; ++f)
            if (strcmp(g_features[f], feature) == 0) return g_featureOn[f];
        return false;
    }
}
