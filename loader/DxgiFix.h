// KenshiPureLoader: early display fix (DXGI scaling), see DxgiFix.cpp.
// Built with the VC++ 2010 (v100) toolset; keep to C++ that compiler accepts.
#pragma once

namespace kp
{
    // Hooks Ogre::Root::initialise; the DXGI hooks are put in place there. MinHook must be
    // initialised. Returns false (and logs why) if it could not arm.
    bool DxgiFixArm();
}
