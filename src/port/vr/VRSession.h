#pragma once

#include <cstdint>
#include <fast/interpreter.h>
#include <unordered_map>
#include <vector>


// Stereoscopic rendering and Touch controller input for Meta Quest, built on OpenXR.
// Only compiled in when SPAGHETTIKART_OPENXR is defined; otherwise every call is a no-op.
namespace VR {

// Creates the OpenXR session on first use. Must be called on the thread that owns the GL context.
bool Init();
bool IsActive();

// Display refresh rate of the headset, used as the frame interpolation target.
uint32_t GetRefreshRate();

// See VR_GetHeadForward in VR.h
bool GetHeadForward(float out[3]);

// Renders one displayed frame per matrix replacement set, once for each eye. Returns false if VR is
// not active, in which case the caller should draw the frame normally.
bool RunCommands(Fast::Interpreter* interpreter, Gfx* pool,
                 const std::vector<std::unordered_map<Mtx*, MtxF>>& mtxReplacements);

} // namespace VR
