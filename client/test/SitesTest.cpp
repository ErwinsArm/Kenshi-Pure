// Host-side test of the run-time resolver (client/Sites.cpp) against the Kenshi exe files: nothing of
// the game is loaded or run; the files are only mapped read-only. Built and run by
// scripts/build-plugin.ps1 when the exes are there.
//   sites-test.exe <exe 1.0.65> <exe 1.0.68>
// Known mode: every row with a value for that build must resolve to exactly the table's value.
// Unknown mode (hash withheld): every row the pattern search resolves must equal the known value.

#include <windows.h>
#include <stdio.h>

#include "Sites.h"
#include "Log.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc < 1 + KP_BUILD_COUNT) { wprintf(L"usage: sites-test <exe per build, in sites.tsv order>\n"); return 2; }
    kp::LogOpen(L"sites-test");
    int bad = 0;
    for (int b = 0; b < KP_BUILD_COUNT; ++b)
    {
        kp::SitesResolve(argv[1 + b], kp::g_builds[b].sha256);
        int n = 0;
        for (int i = 0; i < KP_SITE_COUNT; ++i)
        {
            const kp::KpBuildValue& v = kp::g_sites[i].builds[b];
            if (!v.known) continue;
            ++n;
            if (kp::SiteValue(i) != v.value)
            {
                printf("FAIL %s known %s: got 0x%llx, table 0x%llx\n", kp::g_builds[b].label, kp::g_sites[i].name,
                       kp::SiteValue(i), v.value);
                ++bad;
            }
        }
        printf("%s known: %d rows checked\n", kp::g_builds[b].label, n);

        kp::SitesResolve(argv[1 + b], NULL);
        int found = 0;
        for (int i = 0; i < KP_SITE_COUNT; ++i)
        {
            if (!kp::SiteOk(i)) continue;
            ++found;
            const kp::KpBuildValue& v = kp::g_sites[i].builds[b];
            if (v.known && kp::SiteValue(i) != v.value)
            {
                printf("FAIL %s unknown-mode %s: got 0x%llx, table 0x%llx\n", kp::g_builds[b].label, kp::g_sites[i].name,
                       kp::SiteValue(i), v.value);
                ++bad;
            }
        }
        printf("%s unknown-mode: %d/%d rows found by pattern search, all equal to the table\n", kp::g_builds[b].label,
               found, KP_SITE_COUNT);
    }
    printf(bad ? "sites-test: %d FAILED\n" : "sites-test: ok\n", bad);
    return bad ? 1 : 0;
}
