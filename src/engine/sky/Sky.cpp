#include "Sky.h"

#include <libultraship.h>
#include <libultra/gbi.h>
#include "port/Engine.h"
#include "port/Game.h"

#include "engine/CoreMath.h"
#include "engine/tracks/Track.h"

#include "engine/sky/SkyCloud.h"
#include "engine/sky/SkySnow.h"
#include "engine/sky/SkyStar.h"

extern "C" {
#include "macros.h"
#include "mk64.h"
#include "racing/math_util.h"
#include "racing/skybox_and_splitscreen.h"
#include "menus.h"
}

Vtx Sky::mSkyboxScreenOne[8] = { // D_802B8890
    { { { SCREEN_WIDTH, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
    { { { SCREEN_WIDTH, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
};

Vtx Sky::mSkyboxScreenTwo[8] = { // D_802B8910
    { { { SCREEN_WIDTH, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
    { { { SCREEN_WIDTH, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
};

Vtx Sky::mSkyboxScreenThree[8] = { // D_802B8990
    { { { SCREEN_WIDTH, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
    { { { SCREEN_WIDTH, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
};

Vtx Sky::mSkyboxScreenFour[8] = { // D_802B8A10
    { { { SCREEN_WIDTH, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x1E, 0x1E, 0xFF, 0xFF } } },
    { { { 0, SCREEN_HEIGHT, -1 }, 0, { 0, 0 }, { 0xC8, 0xC8, 0xFF, 0xFF } } },
    { { { SCREEN_WIDTH, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
    { { { SCREEN_WIDTH, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 0, -1 }, 0, { 0, 0 }, { 0x78, 0xFF, 0x78, 0xFF } } },
    { { { 0, 120, -1 }, 0, { 0, 0 }, { 0x00, 0xDC, 0x00, 0xFF } } },
};

Sky* Sky::Instance;

Sky::Sky() {
    Instance = this;
    guMtxIdent(&mSkyboxMatrix);
}

Sky* Sky::GetSky() {
    return Instance;
}

std::vector<std::unique_ptr<SkyActor>>& Sky::GetSkyActors() {
    return mSkyActors;
}

void Sky::SetColours(Vtx* skybox) { // func_802A450C(Vtx* skybox)
    s32 i;

    if (bFog) {

        if (gFogColour.r < 0) {
            gFogColour.r = 0;
        }

        if (gFogColour.g < 0) {
            gFogColour.g = 0;
        }

        if (gFogColour.b < 0) {
            gFogColour.b = 0;
        }

        if (gFogColour.r > 255) {
            gFogColour.r = 255;
        }

        if (gFogColour.g > 255) {
            gFogColour.g = 255;
        }

        if (gFogColour.b > 255) {
            gFogColour.b = 255;
        }

        for (i = 0; i < 8; i++) {
            skybox[i].v.cn[0] = (s16) gFogColour.r;
            skybox[i].v.cn[1] = (s16) gFogColour.g;
            skybox[i].v.cn[2] = (s16) gFogColour.b;
        }
        return;
    }

    SkyboxColours* prop = (SkyboxColours*) &CM_GetProps()->Skybox;

    skybox[0].v.cn[0] = prop->TopRight.r;
    skybox[0].v.cn[1] = prop->TopRight.g;
    skybox[0].v.cn[2] = prop->TopRight.b;

    skybox[1].v.cn[0] = prop->BottomRight.r;
    skybox[1].v.cn[1] = prop->BottomRight.g;
    skybox[1].v.cn[2] = prop->BottomRight.b;

    skybox[2].v.cn[0] = prop->BottomLeft.r;
    skybox[2].v.cn[1] = prop->BottomLeft.g;
    skybox[2].v.cn[2] = prop->BottomLeft.b;

    skybox[3].v.cn[0] = prop->TopLeft.r;
    skybox[3].v.cn[1] = prop->TopLeft.g;
    skybox[3].v.cn[2] = prop->TopLeft.b;

    // Floor
    skybox[4].v.cn[0] = prop->FloorTopRight.r;
    skybox[4].v.cn[1] = prop->FloorTopRight.g;
    skybox[4].v.cn[2] = prop->FloorTopRight.b;

    skybox[5].v.cn[0] = prop->FloorBottomRight.r;
    skybox[5].v.cn[1] = prop->FloorBottomRight.g;
    skybox[5].v.cn[2] = prop->FloorBottomRight.b;

    skybox[6].v.cn[0] = prop->FloorBottomLeft.r;
    skybox[6].v.cn[1] = prop->FloorBottomLeft.g;
    skybox[6].v.cn[2] = prop->FloorBottomLeft.b;

    skybox[7].v.cn[0] = prop->FloorTopLeft.r;
    skybox[7].v.cn[1] = prop->FloorTopLeft.g;
    skybox[7].v.cn[2] = prop->FloorTopLeft.b;
}

void Sky::Draw(ScreenContext* screen) { // func_802A4A0C(Vtx* vtx, ScreenContext* screen)
    Camera* camera = screen->camera;
    s16 temp_t5;
    f32 temp_f0;
    UNUSED s32 pad[2];
    UNUSED u16 pad2;
    u16 sp128;
    Mat4 matrix1 = { 0 };
    Mat4 matrix2 = { 0 };
    Mat4 matrix3 = { 0 };
    Vec3f sp5C;
    f32 sp58;

    Vtx* vtx;
    switch(screen - gScreenContexts) {
        case 0:
            vtx = mSkyboxScreenOne;
            break;
        case 1:
            vtx = mSkyboxScreenTwo;
            break;
        case 2:
            vtx = mSkyboxScreenThree;
            break;        
        case 3:
            vtx = mSkyboxScreenFour;
            break;
    }

    this->SetColours(vtx); //func_802A450C(vtx);
    // Widescreen skybox
    // Note that this is the correct fit for each screen due to how the viewport works
    // +/-8: overshoot the gradient past both edges; this quad's projection
    // path does not preserve the Rect getters' outward rounding, which left
    // the last window column unpainted at some aspect ratios.
    vtx[0].v.ob[0] = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) + 8;
    vtx[1].v.ob[0] = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) + 8;
    vtx[2].v.ob[0] = OTRGetRectDimensionFromLeftEdge(0) - 8;
    vtx[3].v.ob[0] = OTRGetRectDimensionFromLeftEdge(0) - 8;

    vtx[4].v.ob[0] = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) + 8;
    vtx[5].v.ob[0] = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH) + 8;
    vtx[6].v.ob[0] = OTRGetRectDimensionFromLeftEdge(0) - 8;
    vtx[7].v.ob[0] = OTRGetRectDimensionFromLeftEdge(0) - 8;

    sp5C[0] = 0.0f;
    sp5C[1] = 0.0f;
    sp5C[2] = 30000.0f;
    func_802B5564(matrix1, &sp128, camera->unk_B4, gScreenAspect, CM_GetProps()->NearPersp, CM_GetProps()->FarPersp,
                  1.0f);
    func_802B5794(matrix2, camera->pos, camera->lookAt);
    mtxf_multiplication(matrix3, matrix1, matrix2);

    sp58 = ((matrix3[0][3] * sp5C[0]) + (matrix3[1][3] * sp5C[1]) + (matrix3[2][3] * sp5C[2])) + matrix3[3][3];

    mtxf_translate_vec3f_mat4(sp5C, matrix3);

    temp_f0 = (1.0 / sp58);

    sp5C[0] *= temp_f0;
    sp5C[1] *= temp_f0;

    sp5C[0] *= 160.0f;
    sp5C[1] *= 120.0f;

    temp_t5 = 120 - (s32) sp5C[1];
    screen->cameraHeight = temp_t5;
    vtx[1].v.ob[1] = temp_t5;
    vtx[2].v.ob[1] = temp_t5;
    vtx[4].v.ob[1] = temp_t5;
    vtx[7].v.ob[1] = temp_t5;

    init_rdp();
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
    guOrtho(&gGfxPool->mtxScreen, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, 0.0f, 5.0f, 1.0f);
    gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(&gGfxPool->mtxScreen),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, (uintptr_t)&mSkyboxMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(gDisplayListHead++, (uintptr_t)&vtx[0], 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 3, 1, 0, 1, 3, 2, 0);
    if (IsRainbowRoad()) {
        gSPVertex(gDisplayListHead++, (uintptr_t)&vtx[4], 4, 0);
        gSP2Triangles(gDisplayListHead++, 0, 3, 1, 0, 1, 3, 2, 0);
    }
}

void Sky::DrawFloor(ScreenContext* screen) { // func_802A487C
    init_rdp();

    Vtx* vtx;
    switch(screen - gScreenContexts) {
        case 0:
            vtx = mSkyboxScreenOne;
            break;
        case 1:
            vtx = mSkyboxScreenTwo;
            break;
        case 2:
            vtx = mSkyboxScreenThree;
            break;        
        case 3:
            vtx = mSkyboxScreenFour;
            break;
    }

    // Only draw black
    if (IsRainbowRoad()) {
        return;
    }

    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING);
    guOrtho(&gGfxPool->mtxScreen, 0.0f, SCREEN_WIDTH, 0.0f, SCREEN_HEIGHT, 0.0f, 5.0f, 1.0f);
    gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(&gGfxPool->mtxScreen),
                G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, (uintptr_t)&mSkyboxMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(gDisplayListHead++, (uintptr_t)&vtx[4], 4, 0);
    gSP2Triangles(gDisplayListHead++, 0, 3, 1, 0, 1, 3, 2, 0);
}

void Sky::DrawVR(ScreenContext* screen, Mtx* perspective, Mtx* lookAt) {
    Vtx* vtx;
    switch (screen - gScreenContexts) {
        case 1:
            vtx = mSkyboxScreenTwo;
            break;
        case 2:
            vtx = mSkyboxScreenThree;
            break;
        case 3:
            vtx = mSkyboxScreenFour;
            break;
        default:
            vtx = mSkyboxScreenOne;
            break;
    }
    this->SetColours(vtx);

    // The flat sky is a gradient from the top of the screen down to the horizon, then the ground colour
    // (or, on Rainbow Road, more sky) down to the bottom. Map it onto elevation angles instead, from
    // straight down to straight up, with the horizon level with the world.
    auto average = [](const Vtx& a, const Vtx& b, u8 out[4]) {
        for (int i = 0; i < 3; i++) {
            out[i] = (a.v.cn[i] + b.v.cn[i]) / 2;
        }
        out[3] = 0xFF;
    };
    u8 top[4], skyHorizon[4], groundHorizon[4], bottom[4];
    average(vtx[0], vtx[3], top);
    average(vtx[1], vtx[2], skyHorizon);
    average(vtx[4], vtx[7], groundHorizon);
    average(vtx[5], vtx[6], bottom);

    // Elevations in degrees; the gradients span roughly what the original camera shows
    struct Ring {
        f32 elevation;
        const u8* colour;
    };
    const Ring bands[kDomeBands][2] = {
        { { -90.0f, bottom }, { -25.0f, bottom } },
        { { -25.0f, bottom }, { 0.0f, groundHorizon } },
        { { 0.0f, skyHorizon }, { 25.0f, top } },
        { { 25.0f, top }, { 90.0f, top } },
    };

    // Between the near and far planes, and within the range of vertex coordinates
    const f32 radius = MIN(CM_GetProps()->FarPersp * 0.5f, 30000.0f);
    for (int band = 0; band < kDomeBands; band++) {
        for (int chunk = 0; chunk < kDomeSegments / kDomeChunkSegments; chunk++) {
            for (int row = 0; row < 2; row++) {
                const Ring& ring = bands[band][row];
                const f32 elevation = ring.elevation * M_PI / 180.0f;
                for (int col = 0; col <= kDomeChunkSegments; col++) {
                    const f32 azimuth = (chunk * kDomeChunkSegments + col) * 2.0f * M_PI / kDomeSegments;
                    Vtx* v = &mDomeVtx[band][chunk][row * (kDomeChunkSegments + 1) + col];
                    v->v.ob[0] = (s16) (radius * cosf(elevation) * cosf(azimuth));
                    v->v.ob[1] = (s16) (radius * sinf(elevation));
                    v->v.ob[2] = (s16) (radius * cosf(elevation) * sinf(azimuth));
                    v->v.flag = 0;
                    v->v.tc[0] = v->v.tc[1] = 0;
                    for (int i = 0; i < 4; i++) {
                        v->v.cn[i] = ring.colour[i];
                    }
                }
            }
        }
    }

    // Centered on the camera, so only the camera's rotation (and the player's head) moves it
    Camera* camera = screen->camera;
    guTranslate(&mDomeMatrix, camera->pos[0], camera->pos[1], camera->pos[2]);

    init_rdp();
    gDPSetRenderMode(gDisplayListHead++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSPClearGeometryMode(gDisplayListHead++, G_ZBUFFER | G_LIGHTING | G_CULL_BOTH);
    gSPSetGeometryMode(gDisplayListHead++, G_SHADE | G_SHADING_SMOOTH);
    gSPMatrix(gDisplayListHead++, perspective, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, lookAt, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPMatrix(gDisplayListHead++, &mDomeMatrix, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    const int rowLength = kDomeChunkSegments + 1;
    for (int band = 0; band < kDomeBands; band++) {
        for (int chunk = 0; chunk < kDomeSegments / kDomeChunkSegments; chunk++) {
            gSPVertex(gDisplayListHead++, (uintptr_t) mDomeVtx[band][chunk], 2 * rowLength, 0);
            for (int col = 0; col < kDomeChunkSegments; col++) {
                gSP2Triangles(gDisplayListHead++, col, col + 1, rowLength + col + 1, 0, col, rowLength + col + 1,
                              rowLength + col, 0);
            }
        }
    }
}

void Sky::InitActors(ScreenContext* screen) {
    size_t iterations = 0;
    size_t numSnow = 0;
    Track* track = GetWorld()->GetTrack();

    if (!track) {
        printf("[Sky] No track ptr found, skipping sky actors\n");
        return;
    }

    CloudData* cloud = &track->Props.Clouds[0];

    // Handle spawning snow
    if (track->mCloudType == Track::CloudType::SNOW) {
        if (gPlayerCount == 1) {
            numSnow = 50;
        } else {
            numSnow = 25;
        }

        for (size_t i = 0; i < numSnow; i++) {
            mSkyActors.emplace_back(std::make_unique<SkySnow>(screen));
            iterations += 1;
        }
        D_8018D230 = 0;
    }

    // Handle spawning the other cloud types
    if (nullptr != cloud) {
        while ((cloud->rotY != 0xFFFF) && (iterations < 50)) {
            switch(track->mCloudType) {
                case Track::CloudType::NONE:
                case Track::CloudType::SNOW:
                    break;
                case Track::CloudType::CLOUDS: {
                    mSkyActors.emplace_back(std::make_unique<SkyCloud>(screen, cloud->subType, cloud->posY, cloud->rotY, cloud->scalePercent));
                    D_8018D230 = 0;
                    break;
                }
                case Track::CloudType::STARS: {
                    mSkyActors.emplace_back(std::make_unique<SkyStar>(screen, cloud->subType, cloud->posY, cloud->rotY, cloud->scalePercent));
                    D_8018D230 = 1;
                    break;
                }
            }

            cloud++;
            iterations += 1;
        }
    }

    D_8018D1F8 += iterations;
    D_8018D1F0 = iterations;
}

EXTERN_C void InitSkyActors(ScreenContext* screen) {
    Sky::Instance->InitActors(screen);
}

EXTERN_C void DrawSkyActors(ScreenContext* screen, s32 arg0) {
    if (CVarGetInteger("gDrawSkyActors", true) == false) {
        return;
    }

    for (auto& cloud : Sky::Instance->GetSkyActors()) {
        if (cloud->mScreen == screen) {
            cloud->Draw(screen, arg0);
        }
    }
}

EXTERN_C void TickSkyActors() {
    for (auto& cloud : Sky::Instance->GetSkyActors()) {
        cloud->Tick();
    }
}
