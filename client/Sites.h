// KenshiPure's address layer: the table client/sites.tsv (generated into kp_sites_gen.*), resolved
// once at start for the running kenshi_x64.exe. Format and rules: docs/client.md "Address layer".
#pragma once

#include <stddef.h>

#include "kp_sites_gen.h"   // KP_SITE_COUNT, KpSiteId, KP_BUILD_COUNT, KP_FEATURE_COUNT

namespace kp
{
    enum KpSiteKind { SK_CODE, SK_GLOBAL, SK_FIELD, SK_VSLOT, SK_PATCH, SK_CALLSITE, SK_DATA };
    enum KpRead { RD_NONE, RD_RIP, RD_DISP, RD_VTABLE };

    struct KpBuild { const char* label; const char* sha256; const char* note; };

    struct KpBuildValue
    {
        int known;           // 0: no value for this build (unresolved there)
        long long value;     // RVA (code/global/data/patch/callsite), offset (field), slot offset (vslot)
        long long anchor;    // RVA where the pattern's site is (code: = value; vslot: vtable RVA)
    };

    struct KpSiteRow
    {
        const char* name;
        int kind;                    // KpSiteKind
        int feature;                 // index into g_features; 0 = required
        const unsigned char* pat;    // NULL for rows without a pattern
        const unsigned char* mask;   // 1 = compare, 0 = any byte
        int patLen;
        int site;                    // offset of the site inside the pattern
        int read;                    // KpRead
        int readA, readB;            // rip: disp offset, instruction length; disp: offset, size
        KpBuildValue builds[KP_BUILD_COUNT];   // in g_builds order
    };
}


namespace kp
{
    extern const KpBuild g_builds[KP_BUILD_COUNT];
    extern const char* const g_features[KP_FEATURE_COUNT];
    extern const KpSiteRow g_sites[KP_SITE_COUNT];

    // Field offsets per row id; an unresolved field holds a non-canonical offset, so a read through
    // it faults (caught by the caller's __try) instead of returning another field's bytes.
    extern ptrdiff_t g_fieldOff[KP_SITE_COUNT];
    const ptrdiff_t kFieldTrap = (ptrdiff_t)0x4000000000000000LL;

    // Resolves every row for the running exe: patterns are checked in the exe file at exePath (its
    // SHA-256 as hex picks the build; NULL or an unknown hash = pattern search). Addresses point into
    // the loaded exe. Logs build, rows and features. Returns false if a required row is unresolved.
    bool SitesResolve(const wchar_t* exePath, const char* exeSha256);

    const char* SitesBuildName();     // "1.0.65", "1.0.68", or "unknown"
    bool SitesKnownBuild();
    bool SiteOk(int id);
    void* SiteAddress(int id);        // absolute address (code/global/data/patch/callsite), NULL if unresolved
    long long SiteValue(int id);      // resolved RVA / offset / slot offset, -1 if unresolved
    bool FeatureOn(const char* feature);   // e.g. "optional:probe"; "required" is on when SitesResolve passed
    template <typename T> T* SiteGlobal(int id) { return static_cast<T*>(SiteAddress(id)); }

    template <typename T, T M> struct FieldRow;   // specialised per field row in kp_fields_gen.h
}

// Reads or writes a game field through the table: KP_FIELD(ptr, Class, member) is an lvalue of the
// member's declared type at the offset proven for the running build. Raw `ptr->member` on game
// classes is refused by scripts/check-fields.mjs at build time.
#define KP_FIELD(obj, Class, member)                                                                \
    (*reinterpret_cast<decltype(((Class*)0)->member)*>(                                             \
        reinterpret_cast<char*>(obj) +                                                              \
        kp::g_fieldOff[kp::FieldRow<decltype(&Class::member), &Class::member>::id]))
