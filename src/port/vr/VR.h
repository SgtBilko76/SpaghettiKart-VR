#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True while the game is rendering to a VR headset. Callable from the game's C code.
bool VR_IsActive(void);

// The direction the player's head faces, relative to the game camera, in the camera's own space
// (x right, y up, -z forward). Returns false if VR isn't active or tracking isn't available yet.
bool VR_GetHeadForward(float out[3]);

#ifdef __cplusplus
}
#endif
