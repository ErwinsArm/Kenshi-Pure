// The game as KenshiPure sees it: KenshiLib 0.5.0 headers for declarations only (we neither link
// nor load KenshiLib), our address layer for every address and field offset (Sites.h).
//   - Non-virtual game functions: declared by KenshiLib's headers, defined by kp_thunks.asm as
//     `jmp [slot]` with the slot filled by SitesResolve (a trap stub until then).
//   - Virtual calls: the vtable offset is compiled from the headers; each one we use has a vslot row
//     proving that offset per build.
//   - Fields: only through KP_FIELD (raw access is refused by scripts/check-fields.mjs).
//   - Globals: kp::GameWorldGlobal() instead of KenshiLib's `ou` (Globals.h is not used).
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

// Faction.h pulls in Boost.Thread headers, which would auto-link a Boost library we do not build.
// We use no Boost.Thread code, only header-only parts (unordered_set), so switch auto-linking off.
#ifndef BOOST_ALL_NO_LIB
#define BOOST_ALL_NO_LIB
#endif
#ifndef BOOST_SYSTEM_NO_DEPRECATED
#define BOOST_SYSTEM_NO_DEPRECATED   // drops Boost.System's static error categories (need its .lib)
#endif

#include <kenshi/GameWorld.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Character.h>
#include <kenshi/CharMovement.h>
#include <kenshi/Faction.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Inventory.h>
#include <kenshi/Item.h>
#include <kenshi/GameData.h>
#include <kenshi/GameDataManager.h>
#include <kenshi/RootObjectFactory.h>
#include <kenshi/util/hand.h>

#include "Sites.h"
#include "kp_fields_gen.h"

namespace kp
{
    // Kenshi's GameWorld object (a static object in the exe's .data; row "ou").
    inline GameWorld* GameWorldGlobal() { return SiteGlobal<GameWorld>(KPS_ou); }
}
