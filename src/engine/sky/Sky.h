#ifndef SKY_H
#define SKY_H

#ifdef __cplusplus

#include <libultraship/libultraship.h>
#include <libultraship/libultra/gbi.h>

#include "SkyActor.h"

#include "defines.h"

EXTERN_C_START
#include "code_800029B0.h"
EXTERN_C_END

class Sky {
public:
    static Sky* Instance;
    Sky();
    virtual void Draw(ScreenContext* screen);
    virtual void DrawFloor(ScreenContext* screen);
    // Draws the sky gradient as a dome around the camera, so it stays put in VR as the head turns.
    // Uses the camera's projection and lookAt matrices.
    void DrawVR(ScreenContext* screen, Mtx* perspective, Mtx* lookAt);
    Sky* GetSky();
    void SetColours(Vtx* skybox);
    void InitActors(ScreenContext* screen);
    std::vector<std::unique_ptr<SkyActor>>& GetSkyActors();
private:
    Mtx mSkyboxMatrix;
    static Vtx mSkyboxScreenOne[8];
    static Vtx mSkyboxScreenTwo[8];
    static Vtx mSkyboxScreenThree[8];
    static Vtx mSkyboxScreenFour[8];

    std::vector<std::unique_ptr<SkyActor>> mSkyActors;

    static constexpr int kDomeSegments = 16;
    static constexpr int kDomeChunkSegments = 8;
    static constexpr int kDomeBands = 4;
    // Each chunk of a band is two rows of kDomeChunkSegments + 1 vertices
    Vtx mDomeVtx[kDomeBands][kDomeSegments / kDomeChunkSegments][2 * (kDomeChunkSegments + 1)];
    Mtx mDomeMatrix;
};
#endif // __cplusplus

/** C Compatible functions **/
EXTERN_C void InitSkyActors(ScreenContext* screen);
EXTERN_C void TickSkyActors();
EXTERN_C void DrawSkyActors(ScreenContext* screen, s32 arg0);

#endif // SKY_H
