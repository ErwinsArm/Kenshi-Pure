// KenshiPure's address layer at run time (see Sites.h). Runs once, before any hook or game call.
//   Known build (SHA-256 of the exe is in sites.tsv): every row's pattern is checked at the address
//   the table records for that build; an operand read there (global target, field offset) must equal
//   the table's value. Mismatch -> the row is unresolved.
//   Unknown build: each pattern is searched in the running exe's .text; a row needs exactly one hit,
//   and the hits must keep the order of their 1.0.68 addresses (longest ordered subset is kept).
//   Only read-only features run there (features whose name ends in "-write" stay off); vtable slots
//   are not checked there yet, so vslot rows stay unresolved.

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
    bool g_featureOn[KP_FEATURE_COUNT];
    unsigned char* g_base = NULL;
    unsigned long g_textRva = 0, g_textEnd = 0, g_imageSize = 0;
    volatile LONG g_trapLogged[KP_SITE_COUNT];

    bool FindText()
    {
        g_base = reinterpret_cast<unsigned char*>(GetModuleHandleW(NULL));
        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(g_base);
        IMAGE_NT_HEADERS64* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
        g_imageSize = nt->OptionalHeader.SizeOfImage;
        IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (memcmp(s[i].Name, ".text\0\0\0", 8) == 0)
            {
                g_textRva = s[i].VirtualAddress;
                g_textEnd = s[i].VirtualAddress + s[i].Misc.VirtualSize;
                return true;
            }
        return false;
    }

    bool InText(long long rva, int len) { return rva >= g_textRva && rva + len <= g_textEnd; }

    bool MatchAt(const kp::KpSiteRow& r, long long start)
    {
        if (!InText(start, r.patLen)) return false;
        const unsigned char* p = g_base + start;
        for (int i = 0; i < r.patLen; ++i)
            if (r.mask[i] && p[i] != r.pat[i]) return false;
        return true;
    }

    // Value a row stands for, read at its anchor (the site inside the matched pattern).
    long long ReadValue(const kp::KpSiteRow& r, long long anchor)
    {
        const unsigned char* a = g_base + anchor;
        switch (r.read)
        {
        case kp::RD_RIP: return anchor + r.readB + *reinterpret_cast<const int*>(a + r.readA);
        case kp::RD_DISP: return r.readB == 1 ? (long long)*reinterpret_cast<const signed char*>(a + r.readA)
                                              : (long long)*reinterpret_cast<const int*>(a + r.readA);
        default: return r.kind == kp::SK_FIELD ? 0 : anchor;   // field without displacement = offset 0
        }
    }

    // Target of a vtable slot, following one `jmp rel32` (Kenshi is linked incrementally).
    long long SlotTarget(long long vtable, long long slot)
    {
        if (vtable <= 0 || vtable + slot + 8 > g_imageSize) return -1;
        unsigned long long va = *reinterpret_cast<const unsigned long long*>(g_base + vtable + slot);
        long long rva = (long long)(va - (unsigned long long)g_base);
        if (!InText(rva, 5)) return -1;
        if (g_base[rva] == 0xE9) rva = rva + 5 + *reinterpret_cast<const int*>(g_base + rva + 1);
        return rva;
    }

    void Accept(int i, long long value)
    {
        const kp::KpSiteRow& r = kp::g_sites[i];
        g_ok[i] = true;
        if (r.kind == kp::SK_FIELD) kp::g_fieldOff[i] = (ptrdiff_t)value;
        else if (r.kind != kp::SK_VSLOT) g_addr[i] = g_base + value;
        if (r.kind == kp::SK_CODE) kp_slots[i] = g_addr[i];
    }

    // Known build: check each row where the table says it is.
    void ResolveKnown()
    {
        for (int i = 0; i < KP_SITE_COUNT; ++i)
        {
            const kp::KpSiteRow& r = kp::g_sites[i];
            const kp::KpBuildValue& v = r.builds[g_build];
            if (!v.known) continue;
            if (!r.pat) { Accept(i, v.value); continue; }   // header-only row (no anchor in code)
            if (r.kind == kp::SK_VSLOT)
            {
                long long f = SlotTarget(v.anchor, v.value);
                if (f >= 0 && MatchAt(r, f - r.site)) Accept(i, v.value);
                continue;
            }
            if (!MatchAt(r, v.anchor - r.site)) continue;
            if (ReadValue(r, v.anchor) != v.value) continue;
            Accept(i, v.value);
        }
    }

    // First byte-exact run of a pattern, for a fast memchr-led search.
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

    // Number of hits of a row's pattern in .text (stops at 2); *hit = the first.
    int Scan(const kp::KpSiteRow& r, long long* hit)
    {
        int rs, rl;
        LongestRun(r, &rs, &rl);
        if (rl == 0) return 0;
        int n = 0;
        const unsigned char* p = g_base + g_textRva + rs;
        const unsigned char* end = g_base + g_textEnd - (r.patLen - rs);
        while (p < end)
        {
            p = static_cast<const unsigned char*>(memchr(p, r.pat[rs], end - p));
            if (!p) break;
            if (memcmp(p, r.pat + rs, rl) == 0 && MatchAt(r, (p - g_base) - rs))
            {
                *hit = (p - g_base) - rs;
                if (++n >= 2) break;
            }
            ++p;
        }
        return n;
    }

    struct Hit { long long ref; long long at; int row; };
    bool ByRef(const Hit& a, const Hit& b) { return a.ref < b.ref; }

    // Unknown build: search every pattern; keep unique hits that keep the 1.0.68 order.
    void ResolveUnknown()
    {
        const int ref = KP_BUILD_COUNT - 1;   // newest measured build is the order reference
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
        // Longest subset whose hit addresses increase with the reference addresses (O(n^2), n < 100s).
        size_t n = hits.size();
        std::vector<int> len(n, 1), prev(n, -1);
        int best = -1;
        for (size_t i = 0; i < n; ++i)
        {
            for (size_t j = 0; j < i; ++j)
                if (hits[j].at < hits[i].at && len[j] + 1 > len[i]) { len[i] = len[j] + 1; prev[i] = (int)j; }
            if (best < 0 || len[i] > len[best]) best = (int)i;
        }
        std::vector<bool> keep(n, false);
        for (int k = best; k >= 0; k = prev[k]) keep[k] = true;
        for (size_t k = 0; k < n; ++k)
        {
            if (!keep[k]) { kp::Log("sites: %s dropped (out of order)", kp::g_sites[hits[k].row].name); continue; }
            Accept(hits[k].row, ReadValue(kp::g_sites[hits[k].row], hits[k].at));
        }
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
    bool SitesResolve(const char* exeSha256)
    {
        for (int i = 0; i < KP_SITE_COUNT; ++i) { g_ok[i] = false; g_addr[i] = NULL; g_fieldOff[i] = kFieldTrap; }
        if (!FindText()) { Log("sites: no .text section in the exe"); return false; }
        g_build = -1;
        for (int b = 0; b < KP_BUILD_COUNT; ++b)
            if (exeSha256 && strcmp(exeSha256, g_builds[b].sha256) == 0) g_build = b;

        ULONGLONG t0 = GetTickCount64();
        if (g_build >= 0) ResolveKnown(); else ResolveUnknown();
        ULONGLONG ms = GetTickCount64() - t0;

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

    bool FeatureOn(const char* feature)
    {
        if (!g_resolved) return false;
        for (int f = 0; f < KP_FEATURE_COUNT; ++f)
            if (strcmp(g_features[f], feature) == 0) return g_featureOn[f];
        return false;
    }
}
