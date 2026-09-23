#include "VRSession.h"
#include "VR.h"

#ifndef SPAGHETTIKART_OPENXR

namespace VR {
bool Init() {
    return false;
}
bool IsActive() {
    return false;
}
uint32_t GetRefreshRate() {
    return 0;
}
bool RunCommands(Fast::Interpreter*, Gfx*, const std::vector<std::unordered_map<Mtx*, MtxF>>&) {
    return false;
}
bool GetHeadForward(float[3]) {
    return false;
}
} // namespace VR

#else

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <SDL2/SDL.h>
#include <spdlog/spdlog.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace VR {

namespace {

// Game units per meter. Controls how large the world feels and how strong the stereo effect is.
constexpr float kDefaultWorldScale = 12.0f;
// The HUD and menus are drawn on a panel this far away and this wide, in meters.
constexpr float kDefaultPanelDistance = 2.0f;
constexpr float kDefaultPanelWidth = 2.4f;
// Orthographic projections with this z scale are the game's sky gradient, which fills the view.
constexpr float kSkyOrthoZScale = -2.0f / 5.0f;

#define XR_CHECK(call)                                                        \
    do {                                                                      \
        XrResult xrCheckResult = (call);                                      \
        if (XR_FAILED(xrCheckResult)) {                                       \
            SPDLOG_ERROR("[VR] {} failed: {}", #call, (int)xrCheckResult);    \
            return false;                                                     \
        }                                                                     \
    } while (0)

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> framebuffers;
};

enum Input {
    INPUT_RIGHT_TRIGGER,
    INPUT_LEFT_TRIGGER,
    INPUT_RIGHT_GRIP,
    INPUT_LEFT_GRIP,
    INPUT_A,
    INPUT_B,
    INPUT_X,
    INPUT_Y,
    INPUT_MENU,
    INPUT_LEFT_STICK_CLICK,
    INPUT_RIGHT_STICK_CLICK,
    INPUT_LEFT_STICK,
    INPUT_RIGHT_STICK,
    INPUT_COUNT
};

struct InputDef {
    const char* name;
    XrActionType type;
    const char* binding;
};

const InputDef kInputs[INPUT_COUNT] = {
    { "right_trigger", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/right/input/trigger/value" },
    { "left_trigger", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/left/input/trigger/value" },
    { "right_grip", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/right/input/squeeze/value" },
    { "left_grip", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/left/input/squeeze/value" },
    { "button_a", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/a/click" },
    { "button_b", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/b/click" },
    { "button_x", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/x/click" },
    { "button_y", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/y/click" },
    { "button_menu", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/menu/click" },
    { "left_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/thumbstick/click" },
    { "right_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/thumbstick/click" },
    { "left_stick", XR_ACTION_TYPE_VECTOR2F_INPUT, "/user/hand/left/input/thumbstick" },
    { "right_stick", XR_ACTION_TYPE_VECTOR2F_INPUT, "/user/hand/right/input/thumbstick" },
};

struct State {
    bool initAttempted = false;
    bool initialized = false;
    bool sessionRunning = false;

    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;

    std::array<Swapchain, 2> swapchains;
    std::array<XrView, 2> views;
    GLuint readFramebuffer = 0;
    bool srgbWriteControl = false;
    bool picoProfiles = false;
    float refreshRate = 72.0f;

    XrActionSet actionSet = XR_NULL_HANDLE;
    std::array<XrAction, INPUT_COUNT> actions{};
    SDL_Joystick* gamepad = nullptr;

    // Latest head direction in game camera space, for code that chooses what to draw
    bool headTracked = false;
    float headForward[3] = { 0.0f, 0.0f, -1.0f };

    // Per-eye data used by the projection hook while that eye is rendered
    float eyeView[4][4];
    XrFovf eyeFov;
    float aspectAdjust = 1.0f;
};

State g;

// Column-vector 4x4 math, m[row][col]
void MatMul(float out[4][4], const float a[4][4], const float b[4][4]) {
    float r[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(out, r, sizeof(r));
}

// Inverse of the eye pose: world (game camera space, in game units) to eye space
void ViewFromPose(float out[4][4], const XrPosef& pose, float worldScale) {
    const float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
    const float r[3][3] = {
        { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
        { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
        { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) },
    };
    const float p[3] = { pose.position.x * worldScale, pose.position.y * worldScale, pose.position.z * worldScale };
    for (int i = 0; i < 3; i++) {
        // Transposed rotation, translated by -R^T * p
        out[i][0] = r[0][i];
        out[i][1] = r[1][i];
        out[i][2] = r[2][i];
        out[i][3] = -(r[0][i] * p[0] + r[1][i] * p[1] + r[2][i] * p[2]);
    }
    out[3][0] = out[3][1] = out[3][2] = 0.0f;
    out[3][3] = 1.0f;
}

void ProjectionFromFov(float out[4][4], const XrFovf& fov, float nearZ, float farZ) {
    const float left = tanf(fov.angleLeft), right = tanf(fov.angleRight);
    const float up = tanf(fov.angleUp), down = tanf(fov.angleDown);
    const float width = right - left, height = up - down;
    memset(out, 0, sizeof(float) * 16);
    out[0][0] = 2.0f / width;
    out[0][2] = (right + left) / width;
    out[1][1] = 2.0f / height;
    out[1][2] = (up + down) / height;
    out[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    out[2][3] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    out[3][2] = -1.0f;
}

// Called by the interpreter whenever the game loads a projection matrix. The matrix uses the N64's
// row-vector convention, i.e. it is the transpose of the column-vector matrices above.
Fast::Interpreter::StereoProjection OnProjectionLoad(float matrix[4][4]) {
    using Kind = Fast::Interpreter::StereoProjection;

    if (matrix[2][3] == 0.0f) {
        const bool isSky = fabsf(matrix[2][2] - kSkyOrthoZScale) < 0.001f;
        return isSky ? Kind::Fullscreen : Kind::Panel;
    }

    // Keep the game's depth range, replace its field of view and camera offset with the eye's
    const float s = -matrix[2][3];
    const float a = matrix[2][2] / s, b = matrix[3][2] / s;
    float nearZ = b / (a - 1.0f), farZ = b / (a + 1.0f);
    if (!(nearZ > 0.0f && farZ > nearZ)) {
        nearZ = 10.0f;
        farZ = 10000.0f;
    }

    float projection[4][4];
    ProjectionFromFov(projection, g.eyeFov, nearZ, farZ);
    float eye[4][4];
    MatMul(eye, projection, g.eyeView);

    // The interpreter widens x to the render target's aspect ratio afterwards; undo that in advance
    for (int j = 0; j < 4; j++) {
        eye[0][j] /= g.aspectAdjust;
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = eye[j][i];
        }
    }
    return Kind::Perspective;
}

bool CreateActions() {
    XrActionSetCreateInfo setInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(setInfo.actionSetName, "gameplay");
    strcpy(setInfo.localizedActionSetName, "Gameplay");
    XR_CHECK(xrCreateActionSet(g.instance, &setInfo, &g.actionSet));

    std::vector<XrActionSuggestedBinding> bindings;
    for (int i = 0; i < INPUT_COUNT; i++) {
        XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
        info.actionType = kInputs[i].type;
        strcpy(info.actionName, kInputs[i].name);
        strcpy(info.localizedActionName, kInputs[i].name);
        XR_CHECK(xrCreateAction(g.actionSet, &info, &g.actions[i]));

        XrPath path;
        XR_CHECK(xrStringToPath(g.instance, kInputs[i].binding, &path));
        bindings.push_back({ g.actions[i], path });
    }

    // PICO controllers have the same layout as Touch controllers but their own profiles, which only
    // exist when the runtime offers XR_BD_controller_interaction. A runtime rejects a whole profile if it
    // doesn't support one of its paths, so suggest each separately and keep whatever is accepted.
    std::vector<const char*> profiles = { "/interaction_profiles/oculus/touch_controller" };
    if (g.picoProfiles) {
        profiles.push_back("/interaction_profiles/bytedance/pico4_controller");
        profiles.push_back("/interaction_profiles/bytedance/pico_neo3_controller");
    }

    int suggestedProfiles = 0;
    for (const char* profileName : profiles) {
        XrPath profile;
        XR_CHECK(xrStringToPath(g.instance, profileName, &profile));
        XrInteractionProfileSuggestedBinding suggested{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        suggested.interactionProfile = profile;
        suggested.suggestedBindings = bindings.data();
        suggested.countSuggestedBindings = (uint32_t)bindings.size();
        const XrResult result = xrSuggestInteractionProfileBindings(g.instance, &suggested);
        if (XR_SUCCEEDED(result)) {
            suggestedProfiles++;
        } else {
            SPDLOG_WARN("[VR] Runtime rejected bindings for {}: {}", profileName, (int)result);
        }
    }
    if (suggestedProfiles == 0) {
        SPDLOG_ERROR("[VR] No controller bindings were accepted");
        return false;
    }

    XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.countActionSets = 1;
    attach.actionSets = &g.actionSet;
    XR_CHECK(xrAttachSessionActionSets(g.session, &attach));
    return true;
}

// Touch controllers appear to the game as an ordinary SDL gamepad, so the existing controller
// mapping (and rebinding) applies to them.
void CreateVirtualGamepad() {
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    desc.name = "Quest Touch Controllers";

    const int index = SDL_JoystickAttachVirtualEx(&desc);
    if (index < 0) {
        SPDLOG_ERROR("[VR] Could not create virtual gamepad: {}", SDL_GetError());
        return;
    }
    g.gamepad = SDL_JoystickOpen(index);
    SPDLOG_INFO("[VR] Virtual gamepad created (is gamecontroller: {})", SDL_IsGameController(index) ? "yes" : "no");
}

// libultraship handles gamepads being connected in an ImGui window update, which doesn't run in VR.
// Do the same here, so the virtual Touch gamepad (and any Bluetooth gamepad) reaches the game.
void ProcessGamepadHotplug() {
    SDL_PumpEvents();
    auto devices = Ship::Context::GetRawInstance()->GetControlDeck()->GetConnectedPhysicalDeviceManager();
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEADDED) > 0) {
        devices->HandlePhysicalDeviceConnect(event.cdevice.which);
    }
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_CONTROLLERDEVICEREMOVED, SDL_CONTROLLERDEVICEREMOVED) > 0) {
        devices->HandlePhysicalDeviceDisconnect(event.cdevice.which);
    }
}

float GetFloat(Input input) {
    XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
    info.action = g.actions[input];
    XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
    xrGetActionStateFloat(g.session, &info, &state);
    return state.isActive ? state.currentState : 0.0f;
}

bool GetBool(Input input) {
    XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
    info.action = g.actions[input];
    XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
    xrGetActionStateBoolean(g.session, &info, &state);
    return state.isActive && state.currentState;
}

XrVector2f GetStick(Input input) {
    XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
    info.action = g.actions[input];
    XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
    xrGetActionStateVector2f(g.session, &info, &state);
    return state.isActive ? state.currentState : XrVector2f{ 0.0f, 0.0f };
}

Sint16 ToAxis(float value) {
    return (Sint16)std::clamp(value * 32767.0f, -32768.0f, 32767.0f);
}

// Raw trigger axes use the full range, with -32768 meaning released
Sint16 ToTriggerAxis(float value) {
    return (Sint16)std::clamp(value * 65535.0f - 32768.0f, -32768.0f, 32767.0f);
}

void UpdateInput() {
    if (g.actionSet == XR_NULL_HANDLE || g.sessionState != XR_SESSION_STATE_FOCUSED) {
        return;
    }

    XrActiveActionSet active{ g.actionSet, XR_NULL_PATH };
    XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO };
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(g.session, &sync)) || g.gamepad == nullptr) {
        return;
    }

    // The right trigger accelerates, like the A button
    const bool accelerate = GetBool(INPUT_A) || GetFloat(INPUT_RIGHT_TRIGGER) > 0.5f;
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_A, accelerate);
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_X, GetBool(INPUT_B));
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_B, GetBool(INPUT_X));
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_Y, GetBool(INPUT_Y));
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_START, GetBool(INPUT_MENU));
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, GetFloat(INPUT_RIGHT_GRIP) > 0.5f);
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GetFloat(INPUT_LEFT_GRIP) > 0.5f);
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_LEFTSTICK, GetBool(INPUT_LEFT_STICK_CLICK));
    SDL_JoystickSetVirtualButton(g.gamepad, SDL_CONTROLLER_BUTTON_RIGHTSTICK, GetBool(INPUT_RIGHT_STICK_CLICK));
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, ToTriggerAxis(GetFloat(INPUT_LEFT_TRIGGER)));
    // The right trigger is used as the A button above; keep its axis released so it doesn't also press R
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, ToTriggerAxis(0.0f));

    const XrVector2f left = GetStick(INPUT_LEFT_STICK);
    const XrVector2f right = GetStick(INPUT_RIGHT_STICK);
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_LEFTX, ToAxis(left.x));
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_LEFTY, ToAxis(-left.y));
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_RIGHTX, ToAxis(right.x));
    SDL_JoystickSetVirtualAxis(g.gamepad, SDL_CONTROLLER_AXIS_RIGHTY, ToAxis(-right.y));
}

bool CreateSwapchains() {
    uint32_t viewCount = 0;
    XR_CHECK(xrEnumerateViewConfigurationViews(g.instance, g.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                                               &viewCount, nullptr));
    std::vector<XrViewConfigurationView> configViews(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    XR_CHECK(xrEnumerateViewConfigurationViews(g.instance, g.systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                               viewCount, &viewCount, configViews.data()));
    if (viewCount != 2) {
        SPDLOG_ERROR("[VR] Expected 2 views, got {}", viewCount);
        return false;
    }

    uint32_t formatCount = 0;
    XR_CHECK(xrEnumerateSwapchainFormats(g.session, 0, &formatCount, nullptr));
    std::vector<int64_t> formats(formatCount);
    XR_CHECK(xrEnumerateSwapchainFormats(g.session, formatCount, &formatCount, formats.data()));

    // The game writes sRGB-encoded colors. Store them unchanged in an sRGB swapchain when the
    // automatic linear-to-sRGB conversion on write can be turned off, else use a plain RGBA8 one.
    const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
    g.srgbWriteControl = extensions != nullptr && strstr(extensions, "GL_EXT_sRGB_write_control") != nullptr;
    int64_t format = GL_RGBA8;
    if (g.srgbWriteControl && std::find(formats.begin(), formats.end(), GL_SRGB8_ALPHA8) != formats.end()) {
        format = GL_SRGB8_ALPHA8;
    }

    const float resolutionScale = CVarGetFloat("gVR.ResolutionScale", 1.0f);
    for (int eye = 0; eye < 2; eye++) {
        Swapchain& sc = g.swapchains[eye];
        sc.width = (int32_t)(configViews[eye].recommendedImageRectWidth * resolutionScale);
        sc.height = (int32_t)(configViews[eye].recommendedImageRectHeight * resolutionScale);

        XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = sc.width;
        info.height = sc.height;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        XR_CHECK(xrCreateSwapchain(g.session, &info, &sc.handle));

        uint32_t imageCount = 0;
        XR_CHECK(xrEnumerateSwapchainImages(sc.handle, 0, &imageCount, nullptr));
        sc.images.resize(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
        XR_CHECK(xrEnumerateSwapchainImages(sc.handle, imageCount, &imageCount,
                                            (XrSwapchainImageBaseHeader*)sc.images.data()));

        sc.framebuffers.resize(imageCount);
        glGenFramebuffers(imageCount, sc.framebuffers.data());
        for (uint32_t i = 0; i < imageCount; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, sc.framebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[i].image, 0);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenFramebuffers(1, &g.readFramebuffer);

    SPDLOG_INFO("[VR] Eye resolution {}x{}, format 0x{:x}", g.swapchains[0].width, g.swapchains[0].height,
                (uint32_t)format);
    return true;
}

bool CreateSession() {
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initializeLoader));

    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    jobject localActivity = (jobject)SDL_AndroidGetActivity();
    jobject activity = env->NewGlobalRef(localActivity);
    env->DeleteLocalRef(localActivity);

    XrLoaderInitInfoAndroidKHR loaderInfo{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    loaderInfo.applicationVM = vm;
    loaderInfo.applicationContext = activity;
    XR_CHECK(initializeLoader((XrLoaderInitInfoBaseHeaderKHR*)&loaderInfo));

    uint32_t extensionCount = 0;
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr));
    std::vector<XrExtensionProperties> available(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
    XR_CHECK(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, available.data()));
    auto hasExtension = [&](const char* name) {
        for (auto& ext : available) {
            if (strcmp(ext.extensionName, name) == 0) {
                return true;
            }
        }
        return false;
    };

    std::vector<const char*> extensions = { XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                                            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME };
    const bool hasRefreshRate = hasExtension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (hasRefreshRate) {
        extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    }
    // PICO controller profiles
    g.picoProfiles = hasExtension("XR_BD_controller_interaction");
    if (g.picoProfiles) {
        extensions.push_back("XR_BD_controller_interaction");
    }

    XrInstanceCreateInfoAndroidKHR androidInfo{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM = vm;
    androidInfo.applicationActivity = activity;

    XrInstanceCreateInfo instanceInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.next = &androidInfo;
    strcpy(instanceInfo.applicationInfo.applicationName, "Spaghetti Kart");
    strcpy(instanceInfo.applicationInfo.engineName, "libultraship");
    instanceInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    instanceInfo.enabledExtensionCount = (uint32_t)extensions.size();
    instanceInfo.enabledExtensionNames = extensions.data();
    XR_CHECK(xrCreateInstance(&instanceInfo, &g.instance));

    XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_CHECK(xrGetSystem(g.instance, &systemInfo, &g.systemId));

    // Required by the spec before creating an OpenGL ES session
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
    XR_CHECK(xrGetInstanceProcAddr(g.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                                   (PFN_xrVoidFunction*)&getRequirements));
    XrGraphicsRequirementsOpenGLESKHR requirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    XR_CHECK(getRequirements(g.instance, g.systemId, &requirements));

    // Share SDL's GL context with the runtime
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLint configId = 0;
    eglQueryContext(display, context, EGL_CONFIG_ID, &configId);
    const EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    eglChooseConfig(display, configAttribs, &config, 1, &configCount);

    XrGraphicsBindingOpenGLESAndroidKHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = display;
    binding.config = config;
    binding.context = context;

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = g.systemId;
    XR_CHECK(xrCreateSession(g.instance, &sessionInfo, &g.session));

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    XR_CHECK(xrCreateReferenceSpace(g.session, &spaceInfo, &g.localSpace));

    if (hasRefreshRate) {
        PFN_xrGetDisplayRefreshRateFB getRefreshRate = nullptr;
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(g.instance, "xrGetDisplayRefreshRateFB",
                                               (PFN_xrVoidFunction*)&getRefreshRate))) {
            getRefreshRate(g.session, &g.refreshRate);
        }
    }

    if (!CreateSwapchains() || !CreateActions()) {
        return false;
    }
    CreateVirtualGamepad();

    for (auto& view : g.views) {
        view = { XR_TYPE_VIEW };
    }
    SPDLOG_INFO("[VR] OpenXR session created, {} Hz", g.refreshRate);
    return true;
}

void PollEvents() {
    XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(g.instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* changed = (XrEventDataSessionStateChanged*)&event;
                g.sessionState = changed->state;
                if (changed->state == XR_SESSION_STATE_READY) {
                    XrSessionBeginInfo begin{ XR_TYPE_SESSION_BEGIN_INFO };
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    g.sessionRunning = XR_SUCCEEDED(xrBeginSession(g.session, &begin));
                } else if (changed->state == XR_SESSION_STATE_STOPPING) {
                    xrEndSession(g.session);
                    g.sessionRunning = false;
                } else if (changed->state == XR_SESSION_STATE_EXITING ||
                           changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                    SDL_Event quit{ SDL_QUIT };
                    SDL_PushEvent(&quit);
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                SDL_Event quit{ SDL_QUIT };
                SDL_PushEvent(&quit);
                break;
            }
            default:
                break;
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

void SetupEye(Fast::Interpreter* interpreter, int eye) {
    const Swapchain& sc = g.swapchains[eye];
    const XrView& view = g.views[eye];

    const float worldScale = CVarGetFloat("gVR.WorldScale", kDefaultWorldScale);
    ViewFromPose(g.eyeView, view.pose, worldScale);
    g.eyeFov = view.fov;
    g.aspectAdjust = (4.0f / 3.0f) / ((float)sc.width / (float)sc.height);

    // The HUD and menus go on a panel that stays put in the room, in front of where the player faced
    // when the session started (or last recentered). Build the eye's projection of that panel.
    const float distance = CVarGetFloat("gVR.PanelDistance", kDefaultPanelDistance);
    const float panelWidth = CVarGetFloat("gVR.PanelWidth", kDefaultPanelWidth);
    const float panelHeight = panelWidth * 3.0f / 4.0f;
    float panelView[4][4], panelProjection[4][4], panel[4][4];
    ViewFromPose(panelView, view.pose, 1.0f);
    ProjectionFromFov(panelProjection, view.fov, 0.1f, 100.0f);
    MatMul(panel, panelProjection, panelView);

    // Input is clip space after the interpreter's aspect ratio widening, with the 4:3 image at x, y in [-1, 1]
    auto& stereo = interpreter->mStereo;
    const int outputRows[3] = { 0, 1, 3 };
    for (int r = 0; r < 3; r++) {
        const float* row = panel[outputRows[r]];
        stereo.panelMatrix[r][0] = row[0] * panelWidth / (2.0f * g.aspectAdjust);
        stereo.panelMatrix[r][1] = row[1] * panelHeight / 2.0f;
        stereo.panelMatrix[r][2] = row[3] - row[2] * distance;
    }
    stereo.enabled = true;
    stereo.projectionHook = OnProjectionLoad;

    // Render at the swapchain's size into the game framebuffer, which is always offscreen in VR
    interpreter->mCurDimensions.width = sc.width;
    interpreter->mCurDimensions.height = sc.height;
    interpreter->mGameWindowViewport = { 0, 0, 0, 0 };
}

void CopyToSwapchain(GLuint texture, const Swapchain& sc, uint32_t imageIndex) {
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    if (g.srgbWriteControl) {
        glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g.readFramebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sc.framebuffers[imageIndex]);
    // The game framebuffer is stored top row first; OpenXR images are bottom row first
    glBlitFramebuffer(0, 0, sc.width, sc.height, 0, sc.height, sc.width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // The renderer tracks the bound framebuffer itself and expects the default one here
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
}

bool RenderFrame(Fast::Interpreter* interpreter, Gfx* pool, const std::unordered_map<Mtx*, MtxF>& mtxReplacements) {
    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState{ XR_TYPE_FRAME_STATE };
    XR_CHECK(xrWaitFrame(g.session, &waitInfo, &frameState));
    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    XR_CHECK(xrBeginFrame(g.session, &beginInfo));

    UpdateInput();

    std::array<XrCompositionLayerProjectionView, 2> projectionViews{};
    bool rendered = false;

    if (frameState.shouldRender) {
        XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = g.localSpace;
        XrViewState viewState{ XR_TYPE_VIEW_STATE };
        uint32_t viewCount = 0;
        XR_CHECK(xrLocateViews(g.session, &locateInfo, &viewState, 2, &viewCount, g.views.data()));

        const bool tracked = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        if (tracked) {
            // Both eyes share the head's orientation; rotate the forward vector (0, 0, -1) by it
            const XrQuaternionf& q = g.views[0].pose.orientation;
            g.headForward[0] = -2.0f * (q.x * q.z + q.y * q.w);
            g.headForward[1] = -2.0f * (q.y * q.z - q.x * q.w);
            g.headForward[2] = -(1.0f - 2.0f * (q.x * q.x + q.y * q.y));
            g.headTracked = true;

            for (int eye = 0; eye < 2; eye++) {
                const Swapchain& sc = g.swapchains[eye];
                uint32_t imageIndex = 0;
                XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                XR_CHECK(xrAcquireSwapchainImage(sc.handle, &acquireInfo, &imageIndex));
                XrSwapchainImageWaitInfo imageWaitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                imageWaitInfo.timeout = XR_INFINITE_DURATION;
                XR_CHECK(xrWaitSwapchainImage(sc.handle, &imageWaitInfo));

                SetupEye(interpreter, eye);
                interpreter->StartFrame();
                interpreter->Run(pool, mtxReplacements);
                CopyToSwapchain((GLuint)interpreter->mGfxFrameBuffer, sc, imageIndex);

                XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                XR_CHECK(xrReleaseSwapchainImage(sc.handle, &releaseInfo));

                projectionViews[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
                projectionViews[eye].pose = g.views[eye].pose;
                projectionViews[eye].fov = g.views[eye].fov;
                projectionViews[eye].subImage.swapchain = sc.handle;
                projectionViews[eye].subImage.imageRect = { { 0, 0 }, { sc.width, sc.height } };
            }
            interpreter->mStereo.enabled = false;
            interpreter->mRapi->EndFrame();
            rendered = true;
        }
    }

    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    layer.space = g.localSpace;
    layer.viewCount = 2;
    layer.views = projectionViews.data();
    const XrCompositionLayerBaseHeader* layers[] = { (XrCompositionLayerBaseHeader*)&layer };

    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = rendered ? 1 : 0;
    endInfo.layers = layers;
    XR_CHECK(xrEndFrame(g.session, &endInfo));
    return true;
}

} // namespace

bool Init() {
    if (!g.initAttempted) {
        g.initAttempted = true;
        g.initialized = CreateSession();
        if (!g.initialized) {
            SPDLOG_ERROR("[VR] OpenXR initialization failed, falling back to flat rendering");
        }
    }
    return g.initialized;
}

bool IsActive() {
    return g.initialized;
}

uint32_t GetRefreshRate() {
    return (uint32_t)lroundf(g.refreshRate);
}

bool GetHeadForward(float out[3]) {
    if (!g.initialized || !g.headTracked) {
        return false;
    }
    memcpy(out, g.headForward, sizeof(g.headForward));
    return true;
}

bool RunCommands(Fast::Interpreter* interpreter, Gfx* pool,
                 const std::vector<std::unordered_map<Mtx*, MtxF>>& mtxReplacements) {
    if (!Init()) {
        return false;
    }

    ProcessGamepadHotplug();

    for (const auto& mtxStack : mtxReplacements) {
        PollEvents();
        if (!g.sessionRunning) {
            // Nothing is displayed (e.g. headset removed); keep the game at roughly real-time speed
            std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(1e6f / g.refreshRate)));
        } else if (!RenderFrame(interpreter, pool, mtxStack)) {
            SPDLOG_ERROR("[VR] Frame submission failed");
        }
        interpreter->mInterpolationIndex++;
    }
    return true;
}

} // namespace VR

#endif

extern "C" bool VR_IsActive(void) {
    return VR::IsActive();
}

extern "C" bool VR_GetHeadForward(float out[3]) {
    return VR::GetHeadForward(out);
}
