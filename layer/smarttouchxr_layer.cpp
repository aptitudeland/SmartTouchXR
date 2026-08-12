#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <GL/gl.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>


#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr const char* kLayerName = "XR_APILAYER_JBG_SmartTouchXR";

struct InstanceDispatch {
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
};

struct HandTrackerDispatch {
    PFN_xrDestroyHandTrackerEXT destroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints = nullptr;
    XrHandEXT hand = XR_HAND_LEFT_EXT;
    uint64_t locateCallCount = 0;
    bool loggedFirstSuccessfulLocate = false;
};

struct SwapchainState {
    XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    uint32_t imageCount = 0;
    uint32_t eyeIndex = 0;
    uint64_t acquireCallCount = 0;
    uint64_t waitCallCount = 0;
    uint64_t releaseCallCount = 0;
    int64_t lastAcquiredIndex = -1;
    std::vector<ID3D11RenderTargetView*> renderTargetViews;
};

std::mutex gStateMutex;
std::mutex gLogMutex;
PFN_xrGetInstanceProcAddr gNextGetInstanceProcAddr = nullptr;
PFN_xrCreateApiLayerInstance gNextCreateApiLayerInstance = nullptr;
PFN_xrCreateHandTrackerEXT gNextCreateHandTracker = nullptr;
PFN_xrDestroyHandTrackerEXT gNextDestroyHandTracker = nullptr;
PFN_xrLocateHandJointsEXT gNextLocateHandJoints = nullptr;
PFN_xrEndFrame gNextEndFrame = nullptr;
PFN_xrLocateViews gNextLocateViews = nullptr;
PFN_xrCreateReferenceSpace gNextCreateReferenceSpace = nullptr;
PFN_xrDestroySpace gNextDestroySpace = nullptr;
PFN_xrCreateSession gNextCreateSession = nullptr;
PFN_xrCreateSwapchain gNextCreateSwapchain = nullptr;
PFN_xrDestroySwapchain gNextDestroySwapchain = nullptr;
PFN_xrEnumerateSwapchainImages gNextEnumerateSwapchainImages = nullptr;
PFN_xrAcquireSwapchainImage gNextAcquireSwapchainImage = nullptr;
PFN_xrWaitSwapchainImage gNextWaitSwapchainImage = nullptr;
PFN_xrReleaseSwapchainImage gNextReleaseSwapchainImage = nullptr;
uint64_t gEndFrameCallCount = 0;
bool gLoggedCreateHandTrackerIntercept = false;
bool gLoggedDestroyHandTrackerIntercept = false;
bool gLoggedLocateHandJointsIntercept = false;
bool gLoggedEndFrameIntercept = false;
bool gLoggedLocateViewsIntercept = false;
bool gLoggedCreateReferenceSpaceIntercept = false;
bool gLoggedDestroySpaceIntercept = false;
bool gLoggedLocateHandJointsRequest = false;
bool gLoggedCreateSessionIntercept = false;
bool gLoggedCreateSwapchainIntercept = false;
bool gLoggedDestroySwapchainIntercept = false;
bool gLoggedEnumerateSwapchainImagesIntercept = false;
bool gLoggedAcquireSwapchainImageIntercept = false;
bool gLoggedWaitSwapchainImageIntercept = false;
bool gLoggedReleaseSwapchainImageIntercept = false;
std::unordered_map<XrInstance, InstanceDispatch> gInstanceDispatch;
std::unordered_map<XrHandTrackerEXT, HandTrackerDispatch> gHandTrackerDispatch;
std::unordered_map<XrSpace, XrReferenceSpaceType> gReferenceSpaceTypes;
std::unordered_map<XrSwapchain, SwapchainState> gSwapchainStates;
uint32_t gNextEyeIndex = 0;
bool gLoggedIndexTipCubeDraw = false;
bool gLoggedIndexTipTrackingReady = false;
bool gProximityTargetInitialized = false;
XrVector3f gProximityTarget{0.0f, 0.0f, 0.0f};
bool gIndexInsideProximity = false;
bool gLoggedProximityRender = false;
SOCKET gCalibrationSocket = INVALID_SOCKET;
bool gWinsockInitialized = false;
bool gCalibrationSocketReady = false;
std::array<XrVector3f, 3> gCalibrationPoints{{
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f}
}};
std::array<bool, 3> gCalibrationPointValid{{false, false, false}};
bool gCalibrationFrameReady = false;
bool gCockpitActive = false;
std::string gCockpitAircraftName;
ULONGLONG gCalibrationOverUntilMs = 0;
bool gResetShortcutWasDown = false;
int gSelectedConnectorIndex = -1;
int gLastCalibrationCapturedIndex = -1;
XrVector3f gCalibrationOrigin{0.0f, 0.0f, 0.0f};
XrVector3f gCalibrationAxisX{1.0f, 0.0f, 0.0f};
XrVector3f gCalibrationAxisY{0.0f, 1.0f, 0.0f};
XrVector3f gCalibrationAxisZ{0.0f, 0.0f, 1.0f};
std::array<std::array<float, 3>, 3> gDcsToXrRotation{{
    {{1.0f, 0.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, 1.0f}}
}};
XrVector3f gDcsToXrTranslation{0.0f, 0.0f, 0.0f};
float gCalibrationRmsErrorMeters = 0.0f;
float gCalibrationMaxErrorMeters = 0.0f;
bool gLoggedStereoViewSample = false;
std::array<XrView, 2> gLatestViews{{
    {XR_TYPE_VIEW},
    {XR_TYPE_VIEW}
}};
uint32_t gLatestViewCount = 0;
XrViewStateFlags gLatestViewStateFlags = 0;
XrSpace gLatestViewSpace = XR_NULL_HANDLE;
XrVector3f gLatestRightIndexTip{0.0f, 0.0f, 0.0f};
XrSpace gLatestRightIndexBaseSpace = XR_NULL_HANDLE;
XrTime gLatestRightIndexTime = 0;
XrSpaceLocationFlags gLatestRightIndexFlags = 0;
bool gLatestRightIndexValid = false;

std::string logPath() {
    char localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(std::size(localAppData))
    );

    std::string directory =
        length > 0 ? std::string(localAppData) : std::string(".");

    directory += "\\SmartTouchXR";
    CreateDirectoryA(directory.c_str(), nullptr);

    return directory + "\\SmartTouchXR-layer.txt";
}

void logLine(const std::string& message) {
    std::lock_guard<std::mutex> lock(gLogMutex);
    std::ofstream file(logPath(), std::ios::app);
    if (file) {
        file << message << '\n';
    }
}

bool findInstanceDispatch(XrInstance instance, InstanceDispatch* dispatch) {
    if (instance == XR_NULL_HANDLE || dispatch == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gStateMutex);
    const auto iterator = gInstanceDispatch.find(instance);
    if (iterator == gInstanceDispatch.end()) {
        return false;
    }

    *dispatch = iterator->second;
    return true;
}

void storeInstanceDispatch(XrInstance instance, const InstanceDispatch& dispatch) {
    std::lock_guard<std::mutex> lock(gStateMutex);
    gInstanceDispatch[instance] = dispatch;
}

void removeInstanceDispatch(XrInstance instance) {
    std::lock_guard<std::mutex> lock(gStateMutex);
    gInstanceDispatch.erase(instance);
}

XRAPI_ATTR XrResult XRAPI_CALL layerDestroyInstance(XrInstance instance) {
    InstanceDispatch dispatch{};
    if (!findInstanceDispatch(instance, &dispatch) || dispatch.destroyInstance == nullptr) {
        logLine("xrDestroyInstance: no dispatch table for instance");
        return XR_ERROR_HANDLE_INVALID;
    }

    logLine("Forwarding OpenXR instance destruction");
    const XrResult result = dispatch.destroyInstance(instance);

    logLine(
        std::string("OpenXR instance destruction result: ") +
        std::to_string(static_cast<int>(result))
    );

    // The instance is invalid after a successful destruction. Remove the entry
    // after calling the downstream runtime so the function pointer remains valid.
    if (XR_SUCCEEDED(result)) {
        removeInstanceDispatch(instance);
        logLine("Instance dispatch table removed");
    }

    return result;
}

const char* d3dFeatureLevelName(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1:
            return "11_1";
        case D3D_FEATURE_LEVEL_11_0:
            return "11_0";
        case D3D_FEATURE_LEVEL_10_1:
            return "10_1";
        case D3D_FEATURE_LEVEL_10_0:
            return "10_0";
        case D3D_FEATURE_LEVEL_9_3:
            return "9_3";
        case D3D_FEATURE_LEVEL_9_2:
            return "9_2";
        case D3D_FEATURE_LEVEL_9_1:
            return "9_1";
        default:
            return "unknown";
    }
}

const char* structureTypeName(XrStructureType type) {
    switch (type) {
        case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR:
            return "GRAPHICS_BINDING_D3D11_KHR";
        case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR:
            return "GRAPHICS_BINDING_D3D12_KHR";
        case XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR:
            return "GRAPHICS_BINDING_OPENGL_WIN32_KHR";
        default:
            return "OTHER_STRUCTURE";
    }
}

void logSessionCreateChain(const void* next) {
    if (next == nullptr) {
        logLine("xrCreateSession next chain: empty");
        return;
    }

    const XrBaseInStructure* current =
        reinterpret_cast<const XrBaseInStructure*>(next);
    uint32_t index = 0;

    while (current != nullptr && index < 32) {
        logLine(
            std::string("xrCreateSession next[") +
            std::to_string(index) +
            "]: type=" +
            structureTypeName(current->type) +
            " (" +
            std::to_string(static_cast<int>(current->type)) +
            ")"
        );

        switch (current->type) {
            case XR_TYPE_GRAPHICS_BINDING_D3D11_KHR: {
                const auto* binding =
                    reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(current);
                logLine(
                    std::string("Detected graphics API: D3D11, device=") +
                    (binding->device != nullptr ? "present" : "null")
                );

                if (binding->device != nullptr) {
                    const D3D_FEATURE_LEVEL featureLevel =
                        binding->device->GetFeatureLevel();

                    ID3D11DeviceContext* immediateContext = nullptr;
                    binding->device->GetImmediateContext(&immediateContext);

                    logLine(
                        std::string("D3D11 render context: featureLevel=") +
                        d3dFeatureLevelName(featureLevel) +
                        ", immediateContext=" +
                        (immediateContext != nullptr ? "present" : "null")
                    );

                    if (immediateContext != nullptr) {
                        immediateContext->Release();
                    }
                }
                break;
            }
            case XR_TYPE_GRAPHICS_BINDING_D3D12_KHR: {
                const auto* binding =
                    reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(current);
                logLine(
                    std::string("Detected graphics API: D3D12, device=") +
                    (binding->device != nullptr ? "present" : "null") +
                    ", queue=" +
                    (binding->queue != nullptr ? "present" : "null")
                );
                break;
            }
            case XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR: {
                const auto* binding =
                    reinterpret_cast<const XrGraphicsBindingOpenGLWin32KHR*>(current);
                logLine(
                    std::string("Detected graphics API: OpenGL Win32, hDC=") +
                    (binding->hDC != nullptr ? "present" : "null") +
                    ", hGLRC=" +
                    (binding->hGLRC != nullptr ? "present" : "null")
                );
                break;
            }
            default:
                break;
        }

        current = current->next;
        ++index;
    }

    if (index >= 32) {
        logLine("xrCreateSession next chain stopped after 32 entries");
    }
}

XRAPI_ATTR XrResult XRAPI_CALL layerCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo* createInfo,
    XrSession* session
) {
    PFN_xrCreateSession nextCreateSession = nullptr;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextCreateSession = gNextCreateSession;
    }

    if (nextCreateSession == nullptr) {
        logLine("xrCreateSession: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    logLine("Forwarding xrCreateSession");

    if (createInfo != nullptr) {
        logSessionCreateChain(createInfo->next);
    } else {
        logLine("xrCreateSession received null createInfo");
    }

    const XrResult result = nextCreateSession(instance, createInfo, session);

    logLine(
        std::string("xrCreateSession result: ") +
        std::to_string(static_cast<int>(result)) +
        ", session=" +
        (
            session != nullptr && *session != XR_NULL_HANDLE
                ? "created"
                : "null"
        )
    );

    return result;
}


std::string swapchainLabel(XrSwapchain swapchain) {
    return std::to_string(
        static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(swapchain)
        )
    );
}

XRAPI_ATTR XrResult XRAPI_CALL layerCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo* createInfo,
    XrSwapchain* swapchain
) {
    PFN_xrCreateSwapchain nextCreateSwapchain = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextCreateSwapchain = gNextCreateSwapchain;
    }

    if (nextCreateSwapchain == nullptr) {
        logLine("xrCreateSwapchain: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result = nextCreateSwapchain(session, createInfo, swapchain);

    if (
        XR_SUCCEEDED(result) &&
        createInfo != nullptr &&
        swapchain != nullptr &&
        *swapchain != XR_NULL_HANDLE
    ) {
        SwapchainState state{};
        state.createInfo = *createInfo;
        state.createInfo.next = nullptr;
        state.eyeIndex = gNextEyeIndex++ % 2;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gSwapchainStates[*swapchain] = state;
        }

        logLine(
            std::string("Swapchain created: handle=") +
            swapchainLabel(*swapchain) +
            ", width=" + std::to_string(createInfo->width) +
            ", height=" + std::to_string(createInfo->height) +
            ", arraySize=" + std::to_string(createInfo->arraySize) +
            ", mipCount=" + std::to_string(createInfo->mipCount) +
            ", faceCount=" + std::to_string(createInfo->faceCount) +
            ", sampleCount=" + std::to_string(createInfo->sampleCount) +
            ", format=" + std::to_string(createInfo->format) +
            ", usageFlags=" +
            std::to_string(static_cast<unsigned long long>(createInfo->usageFlags))
        );
    } else {
        logLine(
            std::string("xrCreateSwapchain result: ") +
            std::to_string(static_cast<int>(result))
        );
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerDestroySwapchain(XrSwapchain swapchain) {
    PFN_xrDestroySwapchain nextDestroySwapchain = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextDestroySwapchain = gNextDestroySwapchain;
    }

    if (nextDestroySwapchain == nullptr) {
        logLine("xrDestroySwapchain: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const std::string label = swapchainLabel(swapchain);
    const XrResult result = nextDestroySwapchain(swapchain);

    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        const auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            for (ID3D11RenderTargetView* view : it->second.renderTargetViews) {
                if (view != nullptr) {
                    view->Release();
                }
            }
            gSwapchainStates.erase(it);
        }
    }

    logLine(
        std::string("Swapchain destroyed: handle=") + label +
        ", result=" + std::to_string(static_cast<int>(result))
    );
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerEnumerateSwapchainImages(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t* imageCountOutput,
    XrSwapchainImageBaseHeader* images
) {
    PFN_xrEnumerateSwapchainImages nextEnumerate = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextEnumerate = gNextEnumerateSwapchainImages;
    }

    if (nextEnumerate == nullptr) {
        logLine("xrEnumerateSwapchainImages: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result = nextEnumerate(
        swapchain,
        imageCapacityInput,
        imageCountOutput,
        images
    );

    const uint32_t count = imageCountOutput != nullptr ? *imageCountOutput : 0;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        const auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            it->second.imageCount = count;
        }
    }

    logLine(
        std::string("Swapchain images enumerated: handle=") +
        swapchainLabel(swapchain) +
        ", capacity=" + std::to_string(imageCapacityInput) +
        ", count=" + std::to_string(count) +
        ", result=" + std::to_string(static_cast<int>(result))
    );

    if (
        XR_SUCCEEDED(result) &&
        images != nullptr &&
        imageCapacityInput > 0 &&
        images[0].type == XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR
    ) {
        auto* d3dImages =
            reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
        const uint32_t logCount = (std::min)(imageCapacityInput, count);

        for (uint32_t index = 0; index < logCount; ++index) {
            ID3D11Texture2D* texture = d3dImages[index].texture;
            std::string description =
                std::string("D3D11 swapchain image[") +
                std::to_string(index) +
                "]: texture=" +
                (texture != nullptr ? "present" : "null");

            if (texture != nullptr) {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                description +=
                    ", width=" + std::to_string(desc.Width) +
                    ", height=" + std::to_string(desc.Height) +
                    ", arraySize=" + std::to_string(desc.ArraySize) +
                    ", mipLevels=" + std::to_string(desc.MipLevels) +
                    ", format=" + std::to_string(static_cast<int>(desc.Format)) +
                    ", sampleCount=" + std::to_string(desc.SampleDesc.Count) +
                    ", bindFlags=" + std::to_string(desc.BindFlags);
            }

            logLine(description);

            if (texture != nullptr) {
                ID3D11Device* device = nullptr;
                texture->GetDevice(&device);

                if (device == nullptr) {
                    logLine(
                        std::string("D3D11 RTV validation[") +
                        std::to_string(index) +
                        "]: device=null"
                    );
                    continue;
                }

                D3D11_TEXTURE2D_DESC textureDesc{};
                texture->GetDesc(&textureDesc);

                DXGI_FORMAT viewFormat = textureDesc.Format;
                {
                    std::lock_guard<std::mutex> lock(gStateMutex);
                    const auto stateIt = gSwapchainStates.find(swapchain);
                    if (stateIt != gSwapchainStates.end()) {
                        viewFormat = static_cast<DXGI_FORMAT>(
                            stateIt->second.createInfo.format
                        );
                    }
                }

                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format = viewFormat;
                if (textureDesc.ArraySize > 1) {
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtvDesc.Texture2DArray.MipSlice = 0;
                    rtvDesc.Texture2DArray.FirstArraySlice = 0;
                    rtvDesc.Texture2DArray.ArraySize = textureDesc.ArraySize;
                } else {
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    rtvDesc.Texture2D.MipSlice = 0;
                }

                ID3D11RenderTargetView* renderTargetView = nullptr;
                const HRESULT rtvResult = device->CreateRenderTargetView(
                    texture,
                    &rtvDesc,
                    &renderTargetView
                );

                logLine(
                    std::string("D3D11 RTV validation[") +
                    std::to_string(index) +
                    "]: result=" +
                    (SUCCEEDED(rtvResult) ? "OK" : "FAILED") +
                    ", hresult=" +
                    std::to_string(static_cast<long>(rtvResult)) +
                    ", viewFormat=" +
                    std::to_string(static_cast<int>(viewFormat))
                );

                if (renderTargetView != nullptr) {
                    std::lock_guard<std::mutex> lock(gStateMutex);
                    const auto stateIt = gSwapchainStates.find(swapchain);
                    if (stateIt != gSwapchainStates.end()) {
                        if (stateIt->second.renderTargetViews.size() < logCount) {
                            stateIt->second.renderTargetViews.resize(logCount, nullptr);
                        }
                        if (stateIt->second.renderTargetViews[index] != nullptr) {
                            stateIt->second.renderTargetViews[index]->Release();
                        }
                        stateIt->second.renderTargetViews[index] = renderTargetView;
                        renderTargetView = nullptr;
                    }
                }
                if (renderTargetView != nullptr) {
                    renderTargetView->Release();
                }
                device->Release();
            }
        }
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerAcquireSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo* acquireInfo,
    uint32_t* index
) {
    PFN_xrAcquireSwapchainImage nextAcquire = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextAcquire = gNextAcquireSwapchainImage;
    }

    if (nextAcquire == nullptr) {
        logLine("xrAcquireSwapchainImage: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result = nextAcquire(swapchain, acquireInfo, index);
    uint64_t callCount = 0;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            callCount = ++it->second.acquireCallCount;
            if (XR_SUCCEEDED(result) && index != nullptr) {
                it->second.lastAcquiredIndex = static_cast<int64_t>(*index);
            }
        }
    }

    if (callCount == 1 || (callCount != 0 && callCount % 300 == 0)) {
        logLine(
            std::string("Swapchain acquire sample: handle=") +
            swapchainLabel(swapchain) +
            ", index=" +
            (index != nullptr ? std::to_string(*index) : "null") +
            ", call=" + std::to_string(callCount) +
            ", result=" + std::to_string(static_cast<int>(result))
        );
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerWaitSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo* waitInfo
) {
    PFN_xrWaitSwapchainImage nextWait = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextWait = gNextWaitSwapchainImage;
    }

    if (nextWait == nullptr) {
        logLine("xrWaitSwapchainImage: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result = nextWait(swapchain, waitInfo);
    uint64_t callCount = 0;
    int64_t lastIndex = -1;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            callCount = ++it->second.waitCallCount;
            lastIndex = it->second.lastAcquiredIndex;
        }
    }

    if (callCount == 1 || (callCount != 0 && callCount % 300 == 0)) {
        logLine(
            std::string("Swapchain wait sample: handle=") +
            swapchainLabel(swapchain) +
            ", acquiredIndex=" + std::to_string(lastIndex) +
            ", call=" + std::to_string(callCount) +
            ", result=" + std::to_string(static_cast<int>(result))
        );
    }
    return result;
}

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scale(const Vec3& value, float factor) {
    return {value.x * factor, value.y * factor, value.z * factor};
}

float distanceBetween(const Vec3& a, const Vec3& b) {
    const Vec3 delta = subtract(a, b);
    return std::sqrt(
        delta.x * delta.x +
        delta.y * delta.y +
        delta.z * delta.z
    );
}

Vec3 normalize(const Vec3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.000001f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {value.x / length, value.y / length, value.z / length};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 centroid(const std::array<Vec3, 3>& points) {
    return scale(add(add(points[0], points[1]), points[2]), 1.0f / 3.0f);
}

struct OrthonormalBasis {
    Vec3 x;
    Vec3 y;
    Vec3 z;
    bool valid;
};

OrthonormalBasis buildCalibrationBasis(
    const Vec3& master,
    const Vec3& left,
    const Vec3& right
) {
    const Vec3 x = normalize(subtract(right, left));
    const Vec3 midpoint = scale(add(left, right), 0.5f);
    const Vec3 towardMaster = subtract(master, midpoint);
    const Vec3 yUnnormalized = subtract(
        towardMaster,
        scale(x, dot(towardMaster, x))
    );
    const Vec3 y = normalize(yUnnormalized);
    const Vec3 z = normalize(cross(x, y));

    const bool valid =
        dot(x, x) > 0.99f &&
        dot(y, y) > 0.99f &&
        dot(z, z) > 0.99f;

    return {x, y, z, valid};
}

Vec3 rotateDcsToXr(const Vec3& value) {
    return {
        gDcsToXrRotation[0][0] * value.x +
            gDcsToXrRotation[0][1] * value.y +
            gDcsToXrRotation[0][2] * value.z,
        gDcsToXrRotation[1][0] * value.x +
            gDcsToXrRotation[1][1] * value.y +
            gDcsToXrRotation[1][2] * value.z,
        gDcsToXrRotation[2][0] * value.x +
            gDcsToXrRotation[2][1] * value.y +
            gDcsToXrRotation[2][2] * value.z
    };
}

Vec3 transformDcsToXr(const Vec3& value) {
    return add(
        rotateDcsToXr(value),
        {
            gDcsToXrTranslation.x,
            gDcsToXrTranslation.y,
            gDcsToXrTranslation.z
        }
    );
}

std::string calibrationPath() {
    char localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData))
    );
    std::string directory = length > 0 ? std::string(localAppData) : std::string(".");
    directory += "\\SmartTouchXR";
    CreateDirectoryA(directory.c_str(), nullptr);
    return directory + "\\A10CII-three-point-calibration.txt";
}

void saveCalibrationFrame(
    const std::array<Vec3, 3>& dcsPoints,
    const std::array<Vec3, 3>& predictedXrPoints,
    const std::array<float, 3>& pointErrors
) {
    std::ofstream file(calibrationPath(), std::ios::trunc);
    if (!file) {
        logLine("Failed to save three-point calibration file");
        return;
    }

    file << "module=A-10C_2\n";
    file << "method=rigid_basis_fit_with_centroid_translation\n";
    file << "units=meters\n";
    file << "rotation_row_0="
         << gDcsToXrRotation[0][0] << ','
         << gDcsToXrRotation[0][1] << ','
         << gDcsToXrRotation[0][2] << '\n';
    file << "rotation_row_1="
         << gDcsToXrRotation[1][0] << ','
         << gDcsToXrRotation[1][1] << ','
         << gDcsToXrRotation[1][2] << '\n';
    file << "rotation_row_2="
         << gDcsToXrRotation[2][0] << ','
         << gDcsToXrRotation[2][1] << ','
         << gDcsToXrRotation[2][2] << '\n';
    file << "translation="
         << gDcsToXrTranslation.x << ','
         << gDcsToXrTranslation.y << ','
         << gDcsToXrTranslation.z << '\n';
    file << "rms_error_m=" << gCalibrationRmsErrorMeters << '\n';
    file << "max_error_m=" << gCalibrationMaxErrorMeters << '\n';

    static constexpr const char* kNames[3] = {
        "master_caution",
        "left_mfcd_osb1",
        "right_mfcd_osb1"
    };

    for (size_t index = 0; index < 3; ++index) {
        file << kNames[index] << "_dcs="
             << dcsPoints[index].x << ','
             << dcsPoints[index].y << ','
             << dcsPoints[index].z << '\n';
        file << kNames[index] << "_captured_xr="
             << gCalibrationPoints[index].x << ','
             << gCalibrationPoints[index].y << ','
             << gCalibrationPoints[index].z << '\n';
        file << kNames[index] << "_predicted_xr="
             << predictedXrPoints[index].x << ','
             << predictedXrPoints[index].y << ','
             << predictedXrPoints[index].z << '\n';
        file << kNames[index] << "_error_m=" << pointErrors[index] << '\n';
    }

    logLine(std::string("Three-point rigid transform saved: ") + calibrationPath());
}

void rebuildCalibrationFrameIfComplete() {
    if (!gCalibrationPointValid[0] ||
        !gCalibrationPointValid[1] ||
        !gCalibrationPointValid[2]) {
        return;
    }

    // Coordinates already validated against DCS cockpit highlight coordinates:
    // x = forward/aft, y = up/down, z = right/left.
    const std::array<Vec3, 3> dcsPoints{{
        {0.7405973673f, -0.1893186867f,  0.0295004621f},  // Master Caution
        {0.7524973154f, -0.2948178649f, -0.2870004177f},  // Left MFCD OSB 1
        {0.7524974942f, -0.2948176563f,  0.2099997848f}   // Right MFCD OSB 1
    }};

    const std::array<Vec3, 3> xrPoints{{
        {
            gCalibrationPoints[0].x,
            gCalibrationPoints[0].y,
            gCalibrationPoints[0].z
        },
        {
            gCalibrationPoints[1].x,
            gCalibrationPoints[1].y,
            gCalibrationPoints[1].z
        },
        {
            gCalibrationPoints[2].x,
            gCalibrationPoints[2].y,
            gCalibrationPoints[2].z
        }
    }};

    const OrthonormalBasis dcsBasis = buildCalibrationBasis(
        dcsPoints[0],
        dcsPoints[1],
        dcsPoints[2]
    );
    const OrthonormalBasis xrBasis = buildCalibrationBasis(
        xrPoints[0],
        xrPoints[1],
        xrPoints[2]
    );

    if (!dcsBasis.valid || !xrBasis.valid) {
        logLine(
            "Three-point rigid transform rejected: "
            "points are degenerate or nearly aligned"
        );
        return;
    }

    const Vec3 dcsAxes[3] = {
        dcsBasis.x,
        dcsBasis.y,
        dcsBasis.z
    };
    const Vec3 xrAxes[3] = {
        xrBasis.x,
        xrBasis.y,
        xrBasis.z
    };

    // R = B_xr * transpose(B_dcs), where each basis matrix stores axes
    // as columns. This produces a proper rigid rotation with determinant +1.
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            const float xrComponents[3] = {
                row == 0 ? xrAxes[0].x : (row == 1 ? xrAxes[0].y : xrAxes[0].z),
                row == 0 ? xrAxes[1].x : (row == 1 ? xrAxes[1].y : xrAxes[1].z),
                row == 0 ? xrAxes[2].x : (row == 1 ? xrAxes[2].y : xrAxes[2].z)
            };
            const float dcsComponents[3] = {
                column == 0 ? dcsAxes[0].x : (column == 1 ? dcsAxes[0].y : dcsAxes[0].z),
                column == 0 ? dcsAxes[1].x : (column == 1 ? dcsAxes[1].y : dcsAxes[1].z),
                column == 0 ? dcsAxes[2].x : (column == 1 ? dcsAxes[2].y : dcsAxes[2].z)
            };

            gDcsToXrRotation[row][column] =
                xrComponents[0] * dcsComponents[0] +
                xrComponents[1] * dcsComponents[1] +
                xrComponents[2] * dcsComponents[2];
        }
    }

    const Vec3 dcsCenter = centroid(dcsPoints);
    const Vec3 xrCenter = centroid(xrPoints);
    const Vec3 rotatedDcsCenter = rotateDcsToXr(dcsCenter);
    const Vec3 translation = subtract(xrCenter, rotatedDcsCenter);

    gDcsToXrTranslation = {
        translation.x,
        translation.y,
        translation.z
    };

    std::array<Vec3, 3> predictedXrPoints{};
    std::array<float, 3> pointErrors{};
    float squaredErrorSum = 0.0f;
    float maxError = 0.0f;

    for (size_t index = 0; index < 3; ++index) {
        predictedXrPoints[index] = transformDcsToXr(dcsPoints[index]);
        pointErrors[index] = distanceBetween(
            predictedXrPoints[index],
            xrPoints[index]
        );
        squaredErrorSum += pointErrors[index] * pointErrors[index];
        maxError = std::max(maxError, pointErrors[index]);
    }

    gCalibrationRmsErrorMeters = std::sqrt(squaredErrorSum / 3.0f);
    gCalibrationMaxErrorMeters = maxError;

    // Keep the legacy frame fields useful for current prototype rendering.
    gCalibrationOrigin = {
        gDcsToXrTranslation.x,
        gDcsToXrTranslation.y,
        gDcsToXrTranslation.z
    };
    gCalibrationAxisX = {
        gDcsToXrRotation[0][0],
        gDcsToXrRotation[1][0],
        gDcsToXrRotation[2][0]
    };
    gCalibrationAxisY = {
        gDcsToXrRotation[0][1],
        gDcsToXrRotation[1][1],
        gDcsToXrRotation[2][1]
    };
    gCalibrationAxisZ = {
        gDcsToXrRotation[0][2],
        gDcsToXrRotation[1][2],
        gDcsToXrRotation[2][2]
    };

    gCalibrationFrameReady = true;
    gCalibrationOverUntilMs = GetTickCount64() + 4000ULL;
    gSelectedConnectorIndex = -1;
    gIndexInsideProximity = false;

    logLine("Calibration locked after successful three-point fit");
    logLine("HUD state: CALIBRATION OVER for 4 seconds");

    // Fourth validation point: UFC ENTER. This connector was not used by
    // the three-point fit. If the rendered cube lands on the physical ENTER
    // key, the cockpit-to-OpenXR transform generalizes beyond calibration.
    const Vec3 ufcEnterDcs{
        0.7397546172f,
        -0.1867421865f,
        -0.0041990783f
    };
    const Vec3 transformedUfcEnter = transformDcsToXr(ufcEnterDcs);

    gProximityTarget = {
        transformedUfcEnter.x,
        transformedUfcEnter.y,
        transformedUfcEnter.z
    };
    gProximityTargetInitialized = true;

    logLine(
        std::string("Projected validation target UFC_ENTER: x=") +
        std::to_string(transformedUfcEnter.x) +
        ", y=" +
        std::to_string(transformedUfcEnter.y) +
        ", z=" +
        std::to_string(transformedUfcEnter.z)
    );

    saveCalibrationFrame(dcsPoints, predictedXrPoints, pointErrors);

    logLine(
        std::string("Three-point rigid transform complete: rmsErrorMm=") +
        std::to_string(gCalibrationRmsErrorMeters * 1000.0f) +
        ", maxErrorMm=" +
        std::to_string(gCalibrationMaxErrorMeters * 1000.0f)
    );
    logLine(
        std::string("DCS-to-OpenXR translation: x=") +
        std::to_string(gDcsToXrTranslation.x) +
        ", y=" +
        std::to_string(gDcsToXrTranslation.y) +
        ", z=" +
        std::to_string(gDcsToXrTranslation.z)
    );
}

bool initializeCalibrationUdp() {
    if (gCalibrationSocketReady) {
        return true;
    }
    if (!gWinsockInitialized) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            logLine("UDP calibration: WSAStartup failed");
            return false;
        }
        gWinsockInitialized = true;
    }

    gCalibrationSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (gCalibrationSocket == INVALID_SOCKET) {
        logLine("UDP calibration: socket creation failed");
        return false;
    }

    u_long nonBlocking = 1;
    ioctlsocket(gCalibrationSocket, FIONBIO, &nonBlocking);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(34343);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(gCalibrationSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        logLine(std::string("UDP calibration: bind failed, WSA error=") + std::to_string(WSAGetLastError()));
        closesocket(gCalibrationSocket);
        gCalibrationSocket = INVALID_SOCKET;
        return false;
    }

    gCalibrationSocketReady = true;
    logLine("UDP calibration receiver listening on 127.0.0.1:34343");
    return true;
}


void resetCalibrationState(const char* source) {
    gCalibrationPointValid = {{false, false, false}};
    gCalibrationPoints = {{
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    }};
    gCalibrationFrameReady = false;
    gCalibrationOverUntilMs = 0;
    gSelectedConnectorIndex = -1;
    gLastCalibrationCapturedIndex = -1;
    gIndexInsideProximity = false;
    gCalibrationRmsErrorMeters = 0.0f;
    gCalibrationMaxErrorMeters = 0.0f;

    // Recreate the temporary calibration target relative to the user's
    // current head pose on the next render pass.
    gProximityTargetInitialized = false;

    logLine(
        std::string("Calibration reset requested via ") +
        (source != nullptr ? source : "unknown source")
    );
    logLine("Calibration unlocked; waiting for three calibration points");
}

bool pollCalibrationResetShortcut() {
    const bool controlDown =
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown =
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool rDown =
        (GetAsyncKeyState('R') & 0x8000) != 0;

    const bool shortcutDown = controlDown && shiftDown && rDown;
    const bool pressedNow = shortcutDown && !gResetShortcutWasDown;
    gResetShortcutWasDown = shortcutDown;
    return pressedNow;
}

int calibrationIndexForMessage(const std::string& message) {
    if (message.find("MASTER_CAUTION") != std::string::npos) return 0;
    if (message.find("LEFT_MFCD_OSB1") != std::string::npos) return 1;
    if (message.find("RIGHT_MFCD_OSB1") != std::string::npos) return 2;
    return -1;
}

void pollCalibrationUdp(const XrVector3f& currentIndexTip) {
    if (!initializeCalibrationUdp()) {
        return;
    }

    char buffer[512]{};
    for (;;) {
        sockaddr_in sender{};
        int senderLength = sizeof(sender);
        const int received = recvfrom(
            gCalibrationSocket,
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &senderLength
        );
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                logLine(std::string("UDP calibration receive error=") + std::to_string(error));
            }
            break;
        }
        if (received <= 0) break;

        buffer[received] = '\0';
        const std::string message(buffer, static_cast<size_t>(received));

        if (message.rfind("COCKPIT_ENTERED;", 0) == 0) {
            const std::string aircraft =
                message.substr(std::string("COCKPIT_ENTERED;").size());

            gCockpitActive = true;
            gCockpitAircraftName = aircraft;
            resetCalibrationState("cockpit entry");

            logLine(
                std::string("Cockpit entered: aircraft=") +
                (aircraft.empty() ? "UNKNOWN" : aircraft)
            );
            logLine("SmartTouchXR cockpit interaction enabled");
            continue;
        }

        if (message == "COCKPIT_LEFT") {
            gCockpitActive = false;
            gCockpitAircraftName.clear();
            gSelectedConnectorIndex = -1;
            gIndexInsideProximity = false;
            gProximityTargetInitialized = false;
            gCalibrationOverUntilMs = 0;

            logLine("Cockpit left; SmartTouchXR cockpit interaction disabled");
            continue;
        }

        if (message.find("RESET_CALIBRATION") != std::string::npos) {
            if (gCockpitActive) {
                resetCalibrationState("UDP RESET_CALIBRATION");
            } else {
                logLine("Ignored calibration reset while no cockpit is active");
            }
            continue;
        }

        const int index = calibrationIndexForMessage(message);
        if (index < 0) {
            if (
                message.find("SMARTTOUCHXR_CALIBRATION_HEARTBEAT") ==
                    std::string::npos &&
                message.find("SMARTTOUCHXR_SPECTATOR_HEARTBEAT") ==
                    std::string::npos
            ) {
                logLine(std::string("UDP calibration ignored message: ") + message);
            }
            continue;
        }

        if (!gCockpitActive) {
            logLine("Ignored calibration event while no cockpit is active");
            continue;
        }

        if (gCalibrationFrameReady) {
            static constexpr const char* kFrozenNames[3] = {
                "MASTER_CAUTION", "LEFT_MFCD_OSB1", "RIGHT_MFCD_OSB1"
            };
            logLine(
                std::string("Calibration locked; ignored calibration event ") +
                kFrozenNames[index]
            );
            continue;
        }

        static constexpr const char* kNames[3] = {
            "MASTER_CAUTION", "LEFT_MFCD_OSB1", "RIGHT_MFCD_OSB1"
        };

        int expectedIndex = 0;
        while (
            expectedIndex < 3 &&
            gCalibrationPointValid[static_cast<size_t>(expectedIndex)]
        ) {
            ++expectedIndex;
        }

        if (expectedIndex >= 3) {
            logLine("Calibration has all three points; waiting for final lock");
            continue;
        }

        if (index != expectedIndex) {
            logLine(
                std::string("Ignored unexpected calibration event ") +
                kNames[index] +
                "; expected " +
                kNames[expectedIndex]
            );
            continue;
        }

        gCalibrationPoints[static_cast<size_t>(index)] = currentIndexTip;
        gCalibrationPointValid[static_cast<size_t>(index)] = true;
        gLastCalibrationCapturedIndex = index;

        // During calibration, the orange marker follows the latest point that
        // was actually accepted. This makes the visual feedback progress from
        // Master Caution -> Left MFCD OSB1 -> Right MFCD OSB1.
        gProximityTarget = currentIndexTip;
        gProximityTargetInitialized = true;

        logLine(
            std::string("Captured calibration point ") + kNames[index] +
            ": x=" + std::to_string(currentIndexTip.x) +
            ", y=" + std::to_string(currentIndexTip.y) +
            ", z=" + std::to_string(currentIndexTip.z)
        );
        logLine(
            std::string("Calibration marker moved to ") + kNames[index]
        );

        rebuildCalibrationFrameIfComplete();
    }
}

Vec3 rotateByQuaternion(const XrQuaternionf& q, const Vec3& value) {
    const Vec3 u{q.x, q.y, q.z};
    const float scalar = q.w;
    const float dotUV = u.x * value.x + u.y * value.y + u.z * value.z;
    const float dotUU = u.x * u.x + u.y * u.y + u.z * u.z;
    const Vec3 cross{
        u.y * value.z - u.z * value.y,
        u.z * value.x - u.x * value.z,
        u.x * value.y - u.y * value.x
    };

    return add(
        add(scale(u, 2.0f * dotUV), scale(value, scalar * scalar - dotUU)),
        scale(cross, 2.0f * scalar)
    );
}

Vec3 inverseRotateByQuaternion(const XrQuaternionf& q, const Vec3& value) {
    const XrQuaternionf inverse{-q.x, -q.y, -q.z, q.w};
    return rotateByQuaternion(inverse, value);
}

bool projectWorldPoint(
    const Vec3& worldPoint,
    const XrView& view,
    uint32_t width,
    uint32_t height,
    LONG* pixelX,
    LONG* pixelY
) {
    if (pixelX == nullptr || pixelY == nullptr) {
        return false;
    }

    const Vec3 eyePosition{
        view.pose.position.x,
        view.pose.position.y,
        view.pose.position.z
    };
    const Vec3 relative = subtract(worldPoint, eyePosition);
    const Vec3 eyePoint = inverseRotateByQuaternion(view.pose.orientation, relative);

    // OpenXR looks down negative Z.
    if (eyePoint.z >= -0.05f) {
        return false;
    }

    const float tanLeft = std::tan(view.fov.angleLeft);
    const float tanRight = std::tan(view.fov.angleRight);
    const float tanDown = std::tan(view.fov.angleDown);
    const float tanUp = std::tan(view.fov.angleUp);
    const float horizontalRange = tanRight - tanLeft;
    const float verticalRange = tanUp - tanDown;
    if (horizontalRange <= 0.0f || verticalRange <= 0.0f) {
        return false;
    }

    const float tangentX = eyePoint.x / -eyePoint.z;
    const float tangentY = eyePoint.y / -eyePoint.z;
    const float normalizedX = (tangentX - tanLeft) / horizontalRange;
    const float normalizedY = (tanUp - tangentY) / verticalRange;

    if (
        normalizedX < -0.25f || normalizedX > 1.25f ||
        normalizedY < -0.25f || normalizedY > 1.25f
    ) {
        return false;
    }

    *pixelX = static_cast<LONG>(normalizedX * static_cast<float>(width));
    *pixelY = static_cast<LONG>(normalizedY * static_cast<float>(height));
    return true;
}

void appendLineRectangles(
    std::vector<D3D11_RECT>* rectangles,
    LONG x0,
    LONG y0,
    LONG x1,
    LONG y1,
    LONG width,
    LONG height,
    LONG halfThickness
) {
    if (rectangles == nullptr) {
        return;
    }

    constexpr int kSegments = 18;
    const LONG clampedHalfThickness = std::max<LONG>(1, halfThickness);
    for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments);
        const LONG x = static_cast<LONG>(x0 + (x1 - x0) * t);
        const LONG y = static_cast<LONG>(y0 + (y1 - y0) * t);
        D3D11_RECT rectangle{
            std::max<LONG>(0, x - clampedHalfThickness),
            std::max<LONG>(0, y - clampedHalfThickness),
            std::min<LONG>(width, x + clampedHalfThickness + 1),
            std::min<LONG>(height, y + clampedHalfThickness + 1)
        };
        if (rectangle.right > rectangle.left && rectangle.bottom > rectangle.top) {
            rectangles->push_back(rectangle);
        }
    }
}

void appendProjectedCubeRectangles(
    const Vec3& cubeCenter,
    float halfSize,
    LONG halfThickness,
    const Vec3& right,
    const Vec3& up,
    const Vec3& forward,
    const XrView& eyeView,
    uint32_t width,
    uint32_t height,
    std::vector<D3D11_RECT>* rectangles
) {
    if (rectangles == nullptr) {
        return;
    }

    std::array<Vec3, 8> corners{};
    size_t cornerIndex = 0;
    for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                corners[cornerIndex++] = add(
                    cubeCenter,
                    add(
                        scale(right, static_cast<float>(x) * halfSize),
                        add(
                            scale(up, static_cast<float>(y) * halfSize),
                            scale(forward, static_cast<float>(z) * halfSize)
                        )
                    )
                );
            }
        }
    }

    std::array<LONG, 8> pixelX{};
    std::array<LONG, 8> pixelY{};
    std::array<bool, 8> projected{};
    for (size_t index = 0; index < corners.size(); ++index) {
        projected[index] = projectWorldPoint(
            corners[index], eyeView, width, height, &pixelX[index], &pixelY[index]
        );
    }

    static constexpr std::array<std::array<int, 2>, 12> kEdges{{
        {{0, 1}}, {{0, 2}}, {{0, 4}},
        {{1, 3}}, {{1, 5}},
        {{2, 3}}, {{2, 6}},
        {{3, 7}},
        {{4, 5}}, {{4, 6}},
        {{5, 7}}, {{6, 7}}
    }};

    for (const auto& edge : kEdges) {
        if (!projected[edge[0]] || !projected[edge[1]]) {
            continue;
        }

        appendLineRectangles(
            rectangles,
            pixelX[edge[0]], pixelY[edge[0]],
            pixelX[edge[1]], pixelY[edge[1]],
            static_cast<LONG>(width), static_cast<LONG>(height),
            halfThickness
        );
    }
}



std::array<uint8_t, 7> calibrationHudGlyph(char character) {
    switch (character) {
        case 'A': return {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
        case 'B': return {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
        case 'C': return {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
        case 'E': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
        case 'I': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}};
        case 'L': return {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
        case 'N': return {{0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}};
        case 'O': return {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
        case 'R': return {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
        case 'T': return {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
        case 'V': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
        case 'D': return {{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
        case 'F': return {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
        case 'G': return {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}};
        case 'H': return {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
        case 'M': return {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
        case 'S': return {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
        case 'U': return {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
        case '1': return {{0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}};
        case '2': return {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
        case '3': return {{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
        case '/': return {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}};
        default:  return {{0, 0, 0, 0, 0, 0, 0}};
    }
}

void appendCalibrationHudText3D(
    const std::string& message,
    float verticalOffsetMeters,
    const Vec3& panelCenter,
    const Vec3& panelRight,
    const Vec3& panelUp,
    const XrView& eyeView,
    uint32_t width,
    uint32_t height,
    std::vector<D3D11_RECT>* rectangles
) {
    if (rectangles == nullptr || message.empty()) {
        return;
    }

    // Each bitmap "pixel" is a small square on a virtual plane 1.2 m in
    // front of the user's head. Because the same 3D plane is projected through
    // each XrView, both eyes receive physically consistent stereo disparity.
    constexpr float kPixelSizeMeters = 0.0050f;
    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 7;
    constexpr int kGlyphSpacing = 1;

    const float characterAdvance =
        static_cast<float>(kGlyphWidth + kGlyphSpacing) * kPixelSizeMeters;
    const float textWidth =
        static_cast<float>(message.size()) * characterAdvance -
        static_cast<float>(kGlyphSpacing) * kPixelSizeMeters;

    const Vec3 lineCenter = add(
        panelCenter,
        scale(panelUp, verticalOffsetMeters)
    );
    const Vec3 topLeft = add(
        add(lineCenter, scale(panelRight, -0.5f * textWidth)),
        scale(
            panelUp,
            0.5f * static_cast<float>(kGlyphHeight) * kPixelSizeMeters
        )
    );

    for (size_t characterIndex = 0; characterIndex < message.size(); ++characterIndex) {
        const auto glyph = calibrationHudGlyph(message[characterIndex]);

        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                const uint8_t mask =
                    static_cast<uint8_t>(1u << (kGlyphWidth - 1 - column));
                if ((glyph[static_cast<size_t>(row)] & mask) == 0) {
                    continue;
                }

                const float x =
                    static_cast<float>(characterIndex) * characterAdvance +
                    (static_cast<float>(column) + 0.5f) * kPixelSizeMeters;
                const float y =
                    -(static_cast<float>(row) + 0.5f) * kPixelSizeMeters;

                const Vec3 pixelCenter = add(
                    add(topLeft, scale(panelRight, x)),
                    scale(panelUp, y)
                );

                const float halfPixel = 0.5f * kPixelSizeMeters;
                const std::array<Vec3, 4> corners{{
                    add(pixelCenter, add(scale(panelRight, -halfPixel), scale(panelUp,  halfPixel))),
                    add(pixelCenter, add(scale(panelRight,  halfPixel), scale(panelUp,  halfPixel))),
                    add(pixelCenter, add(scale(panelRight, -halfPixel), scale(panelUp, -halfPixel))),
                    add(pixelCenter, add(scale(panelRight,  halfPixel), scale(panelUp, -halfPixel)))
                }};

                LONG minX = static_cast<LONG>(width);
                LONG minY = static_cast<LONG>(height);
                LONG maxX = 0;
                LONG maxY = 0;
                bool allProjected = true;

                for (const Vec3& corner : corners) {
                    LONG px = 0;
                    LONG py = 0;
                    if (!projectWorldPoint(corner, eyeView, width, height, &px, &py)) {
                        allProjected = false;
                        break;
                    }
                    minX = std::min(minX, px);
                    minY = std::min(minY, py);
                    maxX = std::max(maxX, px);
                    maxY = std::max(maxY, py);
                }

                if (!allProjected) {
                    continue;
                }

                D3D11_RECT rectangle{
                    std::max<LONG>(0, minX),
                    std::max<LONG>(0, minY),
                    std::min<LONG>(static_cast<LONG>(width), maxX + 1),
                    std::min<LONG>(static_cast<LONG>(height), maxY + 1)
                };

                if (
                    rectangle.right > rectangle.left &&
                    rectangle.bottom > rectangle.top
                ) {
                    rectangles->push_back(rectangle);
                }
            }
        }
    }
}


void appendConnectorLabel3D(
    const std::string& message,
    const Vec3& anchorPoint,
    const Vec3& panelRight,
    const Vec3& panelUp,
    const XrView& eyeView,
    uint32_t width,
    uint32_t height,
    std::vector<D3D11_RECT>* rectangles
) {
    if (rectangles == nullptr || message.empty()) {
        return;
    }

    // Tiny label placed a few centimetres above the selected cockpit control.
    // It lives in cockpit/OpenXR 3D space and is projected per-eye.
    constexpr float kPixelSizeMeters = 0.0025f;
    constexpr int kGlyphWidth = 5;
    constexpr int kGlyphHeight = 7;
    constexpr int kGlyphSpacing = 1;

    const float characterAdvance =
        static_cast<float>(kGlyphWidth + kGlyphSpacing) * kPixelSizeMeters;
    const float textWidth =
        static_cast<float>(message.size()) * characterAdvance -
        static_cast<float>(kGlyphSpacing) * kPixelSizeMeters;

    const Vec3 labelCenter = add(anchorPoint, scale(panelUp, 0.022f));
    const Vec3 topLeft = add(
        add(labelCenter, scale(panelRight, -0.5f * textWidth)),
        scale(
            panelUp,
            0.5f * static_cast<float>(kGlyphHeight) * kPixelSizeMeters
        )
    );

    for (size_t characterIndex = 0; characterIndex < message.size(); ++characterIndex) {
        const auto glyph = calibrationHudGlyph(message[characterIndex]);

        for (int row = 0; row < kGlyphHeight; ++row) {
            for (int column = 0; column < kGlyphWidth; ++column) {
                const uint8_t mask =
                    static_cast<uint8_t>(1u << (kGlyphWidth - 1 - column));

                if ((glyph[static_cast<size_t>(row)] & mask) == 0) {
                    continue;
                }

                const float x =
                    static_cast<float>(characterIndex) * characterAdvance +
                    (static_cast<float>(column) + 0.5f) * kPixelSizeMeters;
                const float y =
                    -(static_cast<float>(row) + 0.5f) * kPixelSizeMeters;

                const Vec3 pixelCenter = add(
                    add(topLeft, scale(panelRight, x)),
                    scale(panelUp, y)
                );

                const float halfPixel = 0.5f * kPixelSizeMeters;
                const std::array<Vec3, 4> corners{{
                    add(pixelCenter, add(scale(panelRight, -halfPixel), scale(panelUp,  halfPixel))),
                    add(pixelCenter, add(scale(panelRight,  halfPixel), scale(panelUp,  halfPixel))),
                    add(pixelCenter, add(scale(panelRight, -halfPixel), scale(panelUp, -halfPixel))),
                    add(pixelCenter, add(scale(panelRight,  halfPixel), scale(panelUp, -halfPixel)))
                }};

                LONG minX = static_cast<LONG>(width);
                LONG minY = static_cast<LONG>(height);
                LONG maxX = 0;
                LONG maxY = 0;
                bool allProjected = true;

                for (const Vec3& corner : corners) {
                    LONG px = 0;
                    LONG py = 0;
                    if (!projectWorldPoint(corner, eyeView, width, height, &px, &py)) {
                        allProjected = false;
                        break;
                    }

                    minX = std::min(minX, px);
                    minY = std::min(minY, py);
                    maxX = std::max(maxX, px);
                    maxY = std::max(maxY, py);
                }

                if (!allProjected) {
                    continue;
                }

                D3D11_RECT rectangle{
                    std::max<LONG>(0, minX),
                    std::max<LONG>(0, minY),
                    std::min<LONG>(static_cast<LONG>(width), maxX + 1),
                    std::min<LONG>(static_cast<LONG>(height), maxY + 1)
                };

                if (
                    rectangle.right > rectangle.left &&
                    rectangle.bottom > rectangle.top
                ) {
                    rectangles->push_back(rectangle);
                }
            }
        }
    }
}

void appendProjectedCircleRectangles(
    const Vec3& center,
    float radius,
    LONG halfThickness,
    const Vec3& axisA,
    const Vec3& axisB,
    const XrView& eyeView,
    uint32_t width,
    uint32_t height,
    std::vector<D3D11_RECT>* rectangles
) {
    if (rectangles == nullptr) {
        return;
    }

    constexpr int kSegments = 24;
    constexpr float kTwoPi = 6.28318530717958647692f;

    LONG previousX = 0;
    LONG previousY = 0;
    bool previousProjected = false;

    for (int segment = 0; segment <= kSegments; ++segment) {
        const float angle =
            kTwoPi * static_cast<float>(segment) / static_cast<float>(kSegments);

        const Vec3 point = add(
            center,
            add(
                scale(axisA, std::cos(angle) * radius),
                scale(axisB, std::sin(angle) * radius)
            )
        );

        LONG pixelX = 0;
        LONG pixelY = 0;
        const bool projected =
            projectWorldPoint(point, eyeView, width, height, &pixelX, &pixelY);

        if (segment > 0 && previousProjected && projected) {
            appendLineRectangles(
                rectangles,
                previousX,
                previousY,
                pixelX,
                pixelY,
                static_cast<LONG>(width),
                static_cast<LONG>(height),
                halfThickness
            );
        }

        previousX = pixelX;
        previousY = pixelY;
        previousProjected = projected;
    }
}

void drawProximityPrototype(XrSwapchain swapchain, int64_t imageIndex) {
    ID3D11RenderTargetView* renderTargetView = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t eyeIndex = 0;
    XrView eyeView{XR_TYPE_VIEW};
    XrView leftView{XR_TYPE_VIEW};
    XrView rightView{XR_TYPE_VIEW};
    XrVector3f rightIndexTip{0.0f, 0.0f, 0.0f};
    XrVector3f target{0.0f, 0.0f, 0.0f};
    bool proximityActive = false;
    int activeConnectorIndex = -1;
    float activeConnectorDistance = 0.0f;
    bool logTargetInitialization = false;
    bool logProximityEntered = false;
    bool logProximityExited = false;
    int previousSelectedConnectorIndex = -1;
    bool showCalibrationHud = false;
    bool showCalibrationOverHud = false;
    int calibrationCapturedCount = 0;
    int calibrationExpectedIndex = 0;

    static constexpr std::array<Vec3, 9> kCockpitConnectors{{
        {0.7405973673f, -0.1893186867f,  0.0295004621f},  // Master Caution
        {0.7397546172f, -0.1867421865f, -0.0041990783f},  // UFC ENTER
        {0.7297000885f, -0.2159425467f, -0.0223697796f},  // UFC CLR
        {0.7524973154f, -0.2948178649f, -0.2870004177f},  // Left OSB 1
        {0.7524973750f, -0.2948178649f, -0.2100002468f},  // Left OSB 5
        {0.7331519127f, -0.4045305252f, -0.1702503562f},  // Left OSB 10
        {0.7524974942f, -0.2948176563f,  0.2099997848f},  // Right OSB 1
        {0.7524974942f, -0.2948176861f,  0.2869997919f},  // Right OSB 5
        {0.7331521511f, -0.4045309722f,  0.3267496824f}   // Right OSB 10
    }};

    static constexpr std::array<const char*, 9> kCockpitConnectorNames{{
        "MASTER_CAUTION",
        "UFC_ENTER",
        "UFC_CLR",
        "LEFT_MFCD_OSB1",
        "LEFT_MFCD_OSB5",
        "LEFT_MFCD_OSB10",
        "RIGHT_MFCD_OSB1",
        "RIGHT_MFCD_OSB5",
        "RIGHT_MFCD_OSB10"
    }};

    {
        std::lock_guard<std::mutex> lock(gStateMutex);

        // Cockpit-enter/leave UDP messages must be processed even while the
        // visual interaction layer is inactive. Calibration capture messages
        // still use the latest tracked fingertip once a cockpit is active.
        pollCalibrationUdp(gLatestRightIndexTip);

        if (!gCockpitActive) {
            return;
        }

        const auto it = gSwapchainStates.find(swapchain);
        if (
            it == gSwapchainStates.end() ||
            imageIndex < 0 ||
            static_cast<size_t>(imageIndex) >= it->second.renderTargetViews.size() ||
            gLatestViewCount < 2 ||
            (gLatestViewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
            (gLatestViewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0 ||
            !gLatestRightIndexValid ||
            gLatestViewSpace == XR_NULL_HANDLE ||
            gLatestRightIndexBaseSpace != gLatestViewSpace
        ) {
            return;
        }

        renderTargetView = it->second.renderTargetViews[static_cast<size_t>(imageIndex)];
        if (renderTargetView == nullptr) {
            return;
        }

        width = it->second.createInfo.width;
        height = it->second.createInfo.height;
        eyeIndex = std::min<uint32_t>(it->second.eyeIndex, 1);
        eyeView = gLatestViews[eyeIndex];
        leftView = gLatestViews[0];
        rightView = gLatestViews[1];
        rightIndexTip = gLatestRightIndexTip;

        if (!gProximityTargetInitialized) {
            const Vec3 leftEye{
                leftView.pose.position.x,
                leftView.pose.position.y,
                leftView.pose.position.z
            };
            const Vec3 rightEye{
                rightView.pose.position.x,
                rightView.pose.position.y,
                rightView.pose.position.z
            };
            const Vec3 headCenter = scale(add(leftEye, rightEye), 0.5f);
            const Vec3 initialForward =
                rotateByQuaternion(leftView.pose.orientation, {0.0f, 0.0f, -1.0f});
            const Vec3 initialUp =
                rotateByQuaternion(leftView.pose.orientation, {0.0f, 1.0f, 0.0f});

            // A world-locked target initialized 55 cm in front of the head and
            // 8 cm lower. It remains fixed in LOCAL space after initialization.
            const Vec3 initializedTarget = add(
                add(headCenter, scale(initialForward, 0.55f)),
                scale(initialUp, -0.08f)
            );
            gProximityTarget = {
                initializedTarget.x,
                initializedTarget.y,
                initializedTarget.z
            };
            gProximityTargetInitialized = true;
            logTargetInitialization = true;
        }

        if (pollCalibrationResetShortcut()) {
            resetCalibrationState("Ctrl+Shift+R");
        }

        // While calibration is in progress, pollCalibrationUdp() owns the
        // marker position and moves it to the latest accepted calibration
        // point. Once complete, rebuildCalibrationFrameIfComplete() replaces
        // it with the transformed validation target.
        target = gProximityTarget;
        const Vec3 fingertip{
            rightIndexTip.x,
            rightIndexTip.y,
            rightIndexTip.z
        };

        previousSelectedConnectorIndex = gSelectedConnectorIndex;

        if (gCalibrationFrameReady) {
            constexpr float kEnterRadius = 0.025f;
            constexpr float kExitRadius = 0.035f;

            // Hysteresis: once a connector is selected, keep it until the
            // fingertip moves farther than 35 mm. A new target is acquired
            // only inside 25 mm.
            if (
                gSelectedConnectorIndex >= 0 &&
                static_cast<size_t>(gSelectedConnectorIndex) <
                    kCockpitConnectors.size()
            ) {
                const Vec3 selectedProjected = transformDcsToXr(
                    kCockpitConnectors[
                        static_cast<size_t>(gSelectedConnectorIndex)
                    ]
                );
                const float selectedDistance =
                    distanceBetween(fingertip, selectedProjected);

                activeConnectorDistance = selectedDistance;
                if (selectedDistance <= kExitRadius) {
                    activeConnectorIndex = gSelectedConnectorIndex;
                    proximityActive = true;
                } else {
                    gSelectedConnectorIndex = -1;
                }
            }

            if (gSelectedConnectorIndex < 0) {
                float nearestDistance = 1000.0f;
                int nearestIndex = -1;

                for (size_t index = 0; index < kCockpitConnectors.size(); ++index) {
                    const Vec3 projectedConnector =
                        transformDcsToXr(kCockpitConnectors[index]);
                    const float connectorDistance =
                        distanceBetween(fingertip, projectedConnector);

                    if (connectorDistance < nearestDistance) {
                        nearestDistance = connectorDistance;
                        nearestIndex = static_cast<int>(index);
                    }
                }

                activeConnectorDistance = nearestDistance;
                if (nearestIndex >= 0 && nearestDistance <= kEnterRadius) {
                    gSelectedConnectorIndex = nearestIndex;
                    activeConnectorIndex = nearestIndex;
                    proximityActive = true;
                }
            }

            activeConnectorIndex = gSelectedConnectorIndex;
        } else {
            gSelectedConnectorIndex = -1;
            // Keep the original calibration-reference proximity behavior until
            // all three calibration points have been captured.
            const Vec3 targetPoint{target.x, target.y, target.z};
            constexpr float kActivationRadius = 0.05f;
            proximityActive =
                distanceBetween(fingertip, targetPoint) <= kActivationRadius;
        }

        if (proximityActive != gIndexInsideProximity) {
            gIndexInsideProximity = proximityActive;
            logProximityEntered = proximityActive;
            logProximityExited = !proximityActive;
        }

        showCalibrationHud = !gCalibrationFrameReady;
        showCalibrationOverHud =
            gCalibrationFrameReady &&
            GetTickCount64() < gCalibrationOverUntilMs;

        calibrationCapturedCount = 0;
        for (bool valid : gCalibrationPointValid) {
            if (valid) {
                ++calibrationCapturedCount;
            }
        }
        calibrationExpectedIndex = std::min(2, calibrationCapturedCount);

        renderTargetView->AddRef();
    }

    if (logTargetInitialization) {
        logLine(
            std::string("Master Caution calibration target initialized in LOCAL space: x=") +
            std::to_string(target.x) +
            ", y=" + std::to_string(target.y) +
            ", z=" + std::to_string(target.z) +
            ", activationRadius=0.050000"
        );
        logLine("Waiting for DCS three-point calibration events over UDP");
    }
    if (
        previousSelectedConnectorIndex != activeConnectorIndex &&
        activeConnectorIndex >= 0
    ) {
        logLine(
            std::string("Connector target changed: ") +
            kCockpitConnectorNames[static_cast<size_t>(activeConnectorIndex)] +
            ", distanceMm=" +
            std::to_string(activeConnectorDistance * 1000.0f)
        );
    }
    if (
        previousSelectedConnectorIndex >= 0 &&
        activeConnectorIndex < 0
    ) {
        logLine(
            std::string("Connector target released: ") +
            kCockpitConnectorNames[
                static_cast<size_t>(previousSelectedConnectorIndex)
            ]
        );
    }
    if (logProximityEntered && activeConnectorIndex < 0) {
        logLine("Right index entered calibration proximity target");
    }
    if (logProximityExited && previousSelectedConnectorIndex < 0) {
        logLine("Right index exited calibration proximity target");
    }

    Vec3 forward{};
    Vec3 right{};
    Vec3 up{};

    {
        std::lock_guard<std::mutex> lock(gStateMutex);

        if (gCalibrationFrameReady) {
            // Use the calibrated cockpit basis. The debug cube now remains
            // rigidly aligned with the cockpit instead of tilting with the head.
            right = {
                gCalibrationAxisZ.x,
                gCalibrationAxisZ.y,
                gCalibrationAxisZ.z
            };
            up = {
                gCalibrationAxisY.x,
                gCalibrationAxisY.y,
                gCalibrationAxisY.z
            };
            forward = scale(
                {
                    gCalibrationAxisX.x,
                    gCalibrationAxisX.y,
                    gCalibrationAxisX.z
                },
                -1.0f
            );
        } else {
            // Temporary pre-calibration reference stays aligned with LOCAL axes.
            right = {1.0f, 0.0f, 0.0f};
            up = {0.0f, 1.0f, 0.0f};
            forward = {0.0f, 0.0f, -1.0f};
        }
    }

    const Vec3 fingertipCenter{
        rightIndexTip.x,
        rightIndexTip.y,
        rightIndexTip.z
    };
    const Vec3 targetCenter{target.x, target.y, target.z};

    std::vector<D3D11_RECT> fingertipRectangles;
    std::vector<D3D11_RECT> targetRectangles;
    std::vector<D3D11_RECT> activeTargetRectangles;
    std::vector<D3D11_RECT> connectorLabelRectangles;
    std::vector<D3D11_RECT> calibrationHudRectangles;
    fingertipRectangles.reserve(3 * 24 * 19);
    targetRectangles.reserve(9 * 12 * 19);
    activeTargetRectangles.reserve(12 * 19);
    connectorLabelRectangles.reserve(400);
    calibrationHudRectangles.reserve(500);

    const Vec3 leftEyePosition{
        leftView.pose.position.x,
        leftView.pose.position.y,
        leftView.pose.position.z
    };
    const Vec3 rightEyePosition{
        rightView.pose.position.x,
        rightView.pose.position.y,
        rightView.pose.position.z
    };
    const Vec3 hudHeadCenter =
        scale(add(leftEyePosition, rightEyePosition), 0.5f);
    const Vec3 hudForward =
        rotateByQuaternion(leftView.pose.orientation, {0.0f, 0.0f, -1.0f});
    const Vec3 hudRight =
        rotateByQuaternion(leftView.pose.orientation, {1.0f, 0.0f, 0.0f});
    const Vec3 hudUp =
        rotateByQuaternion(leftView.pose.orientation, {0.0f, 1.0f, 0.0f});

    // Put the virtual text panel 0.8 m ahead and a little above gaze center.
    const Vec3 hudPanelCenter = add(
        add(hudHeadCenter, scale(hudForward, 0.80f)),
        scale(hudUp, 0.08f)
    );

    if (showCalibrationHud) {
        static constexpr std::array<const char*, 3> kCalibrationHudTargets{{
            "MASTER CAUTION",
            "LEFT MFCD OSB1",
            "RIGHT MFCD OSB1"
        }};
        const int nextStep =
            std::min(3, calibrationCapturedCount + 1);

        appendCalibrationHudText3D(
            std::string("CALIBRATION ") +
                std::to_string(nextStep) +
                "/3",
            0.025f,
            hudPanelCenter,
            hudRight,
            hudUp,
            eyeView,
            width,
            height,
            &calibrationHudRectangles
        );
        appendCalibrationHudText3D(
            kCalibrationHudTargets[
                static_cast<size_t>(calibrationExpectedIndex)
            ],
            -0.025f,
            hudPanelCenter,
            hudRight,
            hudUp,
            eyeView,
            width,
            height,
            &calibrationHudRectangles
        );
    } else if (showCalibrationOverHud) {
        appendCalibrationHudText3D(
            "CALIBRATION OVER",
            0.0f,
            hudPanelCenter,
            hudRight,
            hudUp,
            eyeView,
            width,
            height,
            &calibrationHudRectangles
        );
    }

    // Small wireframe sphere at the tracked right-index fingertip.
    // Three orthogonal circles give a clear spherical cue without requiring
    // a new triangle/shader rendering pipeline.
    constexpr float kFingertipSphereRadius = 0.0055f;
    appendProjectedCircleRectangles(
        fingertipCenter, kFingertipSphereRadius, 1,
        right, up, eyeView, width, height, &fingertipRectangles
    );
    appendProjectedCircleRectangles(
        fingertipCenter, kFingertipSphereRadius, 1,
        right, forward, eyeView, width, height, &fingertipRectangles
    );
    appendProjectedCircleRectangles(
        fingertipCenter, kFingertipSphereRadius, 1,
        up, forward, eyeView, width, height, &fingertipRectangles
    );

    bool calibrationReadyForMarkers = false;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        calibrationReadyForMarkers = gCalibrationFrameReady;
    }

    if (calibrationReadyForMarkers) {
        // Marker half-size reduced by 40%: 9.0 mm -> 5.4 mm.
        constexpr float kConnectorMarkerHalfSize = 0.0054f;

        for (size_t index = 0; index < kCockpitConnectors.size(); ++index) {
            const Vec3 projected = transformDcsToXr(kCockpitConnectors[index]);
            std::vector<D3D11_RECT>* destination =
                static_cast<int>(index) == activeConnectorIndex
                    ? &activeTargetRectangles
                    : &targetRectangles;

            appendProjectedCubeRectangles(
                projected,
                kConnectorMarkerHalfSize,
                1,
                right,
                up,
                forward,
                eyeView,
                width,
                height,
                destination
            );
        }
    } else {
        appendProjectedCubeRectangles(
            targetCenter,
            0.0125f,
            1,
            right,
            up,
            forward,
            eyeView,
            width,
            height,
            &targetRectangles
        );
    }

    if (
        calibrationReadyForMarkers &&
        activeConnectorIndex >= 0 &&
        static_cast<size_t>(activeConnectorIndex) < kCockpitConnectors.size()
    ) {
        const Vec3 selectedProjected = transformDcsToXr(
            kCockpitConnectors[static_cast<size_t>(activeConnectorIndex)]
        );

        appendConnectorLabel3D(
            kCockpitConnectorNames[static_cast<size_t>(activeConnectorIndex)],
            selectedProjected,
            right,
            up,
            eyeView,
            width,
            height,
            &connectorLabelRectangles
        );
    }

    if (
        fingertipRectangles.empty() &&
        targetRectangles.empty() &&
        activeTargetRectangles.empty() &&
        connectorLabelRectangles.empty() &&
        calibrationHudRectangles.empty()
    ) {
        renderTargetView->Release();
        return;
    }

    ID3D11Resource* resource = nullptr;
    renderTargetView->GetResource(&resource);
    if (resource == nullptr) {
        renderTargetView->Release();
        return;
    }

    ID3D11Device* device = nullptr;
    resource->GetDevice(&device);
    resource->Release();
    if (device == nullptr) {
        renderTargetView->Release();
        return;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    device->Release();
    if (context == nullptr) {
        renderTargetView->Release();
        return;
    }

    ID3D11DeviceContext1* context1 = nullptr;
    const HRESULT queryResult = context->QueryInterface(
        __uuidof(ID3D11DeviceContext1),
        reinterpret_cast<void**>(&context1)
    );
    context->Release();

    if (FAILED(queryResult) || context1 == nullptr) {
        renderTargetView->Release();
        return;
    }

    const FLOAT fingertipColor[4] = {1.0f, 0.85f, 0.05f, 1.0f};
    const FLOAT targetWaitingColor[4] = {1.0f, 0.35f, 0.05f, 1.0f};
    const FLOAT targetProjectedColor[4] = {0.05f, 0.90f, 1.0f, 1.0f};
    const FLOAT targetActiveColor[4] = {0.15f, 1.0f, 0.20f, 1.0f};
    const FLOAT connectorLabelColor[4] = {0.95f, 0.95f, 0.95f, 1.0f};
    const FLOAT calibrationHudColor[4] = {1.0f, 0.70f, 0.05f, 1.0f};
    const FLOAT calibrationOverHudColor[4] = {0.15f, 1.0f, 0.20f, 1.0f};

    bool calibrationReady = false;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        calibrationReady = gCalibrationFrameReady;
    }

    const FLOAT* targetColor =
        calibrationReady ? targetProjectedColor : targetWaitingColor;

    if (!targetRectangles.empty()) {
        context1->ClearView(
            renderTargetView,
            targetColor,
            targetRectangles.data(),
            static_cast<UINT>(targetRectangles.size())
        );
    }

    if (!activeTargetRectangles.empty()) {
        context1->ClearView(
            renderTargetView,
            targetActiveColor,
            activeTargetRectangles.data(),
            static_cast<UINT>(activeTargetRectangles.size())
        );
    }

    if (!fingertipRectangles.empty()) {
        context1->ClearView(
            renderTargetView,
            fingertipColor,
            fingertipRectangles.data(),
            static_cast<UINT>(fingertipRectangles.size())
        );
    }

    if (!connectorLabelRectangles.empty()) {
        context1->ClearView(
            renderTargetView,
            connectorLabelColor,
            connectorLabelRectangles.data(),
            static_cast<UINT>(connectorLabelRectangles.size())
        );
    }

    if (!calibrationHudRectangles.empty()) {
        context1->ClearView(
            renderTargetView,
            showCalibrationOverHud
                ? calibrationOverHudColor
                : calibrationHudColor,
            calibrationHudRectangles.data(),
            static_cast<UINT>(calibrationHudRectangles.size())
        );
    }

    context1->Release();
    renderTargetView->Release();

    bool shouldLog = false;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        if (!gLoggedProximityRender) {
            gLoggedProximityRender = true;
            shouldLog = true;
        }
    }
    if (shouldLog) {
        logLine(
            "Projected cockpit connector validation rendering successfully "
            "(9 markers after calibration)"
        );
    }
}

XRAPI_ATTR XrResult XRAPI_CALL layerReleaseSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo* releaseInfo
) {
    PFN_xrReleaseSwapchainImage nextRelease = nullptr;
    int64_t lastIndex = -1;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextRelease = gNextReleaseSwapchainImage;
        const auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            lastIndex = it->second.lastAcquiredIndex;
        }
    }

    if (nextRelease == nullptr) {
        logLine("xrReleaseSwapchainImage: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    drawProximityPrototype(swapchain, lastIndex);

    const XrResult result = nextRelease(swapchain, releaseInfo);
    uint64_t callCount = 0;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        auto it = gSwapchainStates.find(swapchain);
        if (it != gSwapchainStates.end()) {
            callCount = ++it->second.releaseCallCount;
            if (XR_SUCCEEDED(result)) {
                it->second.lastAcquiredIndex = -1;
            }
        }
    }

    if (callCount == 1 || (callCount != 0 && callCount % 300 == 0)) {
        logLine(
            std::string("Swapchain release sample: handle=") +
            swapchainLabel(swapchain) +
            ", releasedIndex=" + std::to_string(lastIndex) +
            ", call=" + std::to_string(callCount) +
            ", result=" + std::to_string(static_cast<int>(result))
        );
    }
    return result;
}

const char* referenceSpaceTypeName(XrReferenceSpaceType type) {
    switch (type) {
        case XR_REFERENCE_SPACE_TYPE_VIEW:
            return "VIEW";
        case XR_REFERENCE_SPACE_TYPE_LOCAL:
            return "LOCAL";
        case XR_REFERENCE_SPACE_TYPE_STAGE:
            return "STAGE";
#ifdef XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT
        case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT:
            return "LOCAL_FLOOR_EXT";
#endif
        default:
            return "UNKNOWN";
    }
}

const char* knownSpaceTypeName(XrSpace space) {
    std::lock_guard<std::mutex> lock(gStateMutex);
    const auto iterator = gReferenceSpaceTypes.find(space);
    if (iterator == gReferenceSpaceTypes.end()) {
        return "UNTRACKED_SPACE";
    }

    return referenceSpaceTypeName(iterator->second);
}

XRAPI_ATTR XrResult XRAPI_CALL layerCreateReferenceSpace(
    XrSession session,
    const XrReferenceSpaceCreateInfo* createInfo,
    XrSpace* space
) {
    PFN_xrCreateReferenceSpace nextCreateReferenceSpace = nullptr;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextCreateReferenceSpace = gNextCreateReferenceSpace;
    }

    if (nextCreateReferenceSpace == nullptr) {
        logLine("xrCreateReferenceSpace: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result =
        nextCreateReferenceSpace(session, createInfo, space);

    if (
        XR_SUCCEEDED(result) &&
        createInfo != nullptr &&
        space != nullptr &&
        *space != XR_NULL_HANDLE
    ) {
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gReferenceSpaceTypes[*space] = createInfo->referenceSpaceType;
        }

        logLine(
            std::string("Reference space created: type=") +
            referenceSpaceTypeName(createInfo->referenceSpaceType)
        );
    } else {
        logLine(
            std::string("xrCreateReferenceSpace result: ") +
            std::to_string(static_cast<int>(result))
        );
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerDestroySpace(
    XrSpace space
) {
    PFN_xrDestroySpace nextDestroySpace = nullptr;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextDestroySpace = gNextDestroySpace;
    }

    if (nextDestroySpace == nullptr) {
        logLine("xrDestroySpace: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const char* trackedType = knownSpaceTypeName(space);
    const XrResult result = nextDestroySpace(space);

    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gReferenceSpaceTypes.erase(space);
    }

    if (std::strcmp(trackedType, "UNTRACKED_SPACE") != 0) {
        logLine(
            std::string("Reference space destroyed: type=") +
            trackedType +
            ", result=" +
            std::to_string(static_cast<int>(result))
        );
    }

    return result;
}

const char* handName(XrHandEXT hand) {
    switch (hand) {
        case XR_HAND_LEFT_EXT:
            return "left";
        case XR_HAND_RIGHT_EXT:
            return "right";
        default:
            return "unknown";
    }
}

XRAPI_ATTR XrResult XRAPI_CALL layerCreateHandTrackerEXT(
    XrSession session,
    const XrHandTrackerCreateInfoEXT* createInfo,
    XrHandTrackerEXT* handTracker
) {
    PFN_xrCreateHandTrackerEXT nextCreateHandTracker = nullptr;
    PFN_xrDestroyHandTrackerEXT nextDestroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT nextLocateHandJoints = nullptr;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextCreateHandTracker = gNextCreateHandTracker;
        nextDestroyHandTracker = gNextDestroyHandTracker;
        nextLocateHandJoints = gNextLocateHandJoints;
    }

    if (nextCreateHandTracker == nullptr) {
        logLine("xrCreateHandTrackerEXT: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrHandEXT requestedHand =
        createInfo != nullptr ? createInfo->hand : XR_HAND_LEFT_EXT;

    logLine(
        std::string("Forwarding xrCreateHandTrackerEXT for ") +
        handName(requestedHand) +
        " hand"
    );

    const XrResult result =
        nextCreateHandTracker(session, createInfo, handTracker);

    logLine(
        std::string("xrCreateHandTrackerEXT result for ") +
        handName(requestedHand) +
        ": " +
        std::to_string(static_cast<int>(result))
    );

    if (
        XR_SUCCEEDED(result) &&
        handTracker != nullptr &&
        *handTracker != XR_NULL_HANDLE
    ) {
        HandTrackerDispatch dispatch{};
        dispatch.destroyHandTracker = nextDestroyHandTracker;
        dispatch.locateHandJoints = nextLocateHandJoints;
        dispatch.hand = requestedHand;

        std::lock_guard<std::mutex> lock(gStateMutex);
        gHandTrackerDispatch[*handTracker] = dispatch;

        logLine(
            std::string("Hand tracker dispatch table created for ") +
            handName(requestedHand) +
            " hand"
        );
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerDestroyHandTrackerEXT(
    XrHandTrackerEXT handTracker
) {
    HandTrackerDispatch dispatch{};

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        const auto iterator = gHandTrackerDispatch.find(handTracker);
        if (iterator != gHandTrackerDispatch.end()) {
            dispatch = iterator->second;
        }
    }

    if (dispatch.destroyHandTracker == nullptr) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        dispatch.destroyHandTracker = gNextDestroyHandTracker;
    }

    if (dispatch.destroyHandTracker == nullptr) {
        logLine("xrDestroyHandTrackerEXT: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    logLine(
        std::string("Forwarding xrDestroyHandTrackerEXT for ") +
        handName(dispatch.hand) +
        " hand"
    );

    const XrResult result = dispatch.destroyHandTracker(handTracker);

    logLine(
        std::string("xrDestroyHandTrackerEXT result: ") +
        std::to_string(static_cast<int>(result))
    );

    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gHandTrackerDispatch.erase(handTracker);
        logLine("Hand tracker dispatch table removed");
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerLocateHandJointsEXT(
    XrHandTrackerEXT handTracker,
    const XrHandJointsLocateInfoEXT* locateInfo,
    XrHandJointLocationsEXT* locations
) {
    HandTrackerDispatch dispatch{};
    uint64_t callCount = 0;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        const auto iterator = gHandTrackerDispatch.find(handTracker);
        if (iterator != gHandTrackerDispatch.end()) {
            dispatch = iterator->second;
            callCount = ++iterator->second.locateCallCount;
        }
    }

    if (dispatch.locateHandJoints == nullptr) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        dispatch.locateHandJoints = gNextLocateHandJoints;
    }

    if (dispatch.locateHandJoints == nullptr) {
        logLine("xrLocateHandJointsEXT: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result =
        dispatch.locateHandJoints(handTracker, locateInfo, locations);

    if (XR_FAILED(result)) {
        logLine(
            std::string("xrLocateHandJointsEXT failed for ") +
            handName(dispatch.hand) +
            " hand: " +
            std::to_string(static_cast<int>(result))
        );
        return result;
    }

    if (dispatch.hand == XR_HAND_RIGHT_EXT) {
        const uint32_t indexTip =
            static_cast<uint32_t>(XR_HAND_JOINT_INDEX_TIP_EXT);
        bool valid = false;
        XrVector3f position{0.0f, 0.0f, 0.0f};
        XrSpaceLocationFlags flags = 0;

        if (
            locateInfo != nullptr &&
            locations != nullptr &&
            locations->isActive == XR_TRUE &&
            locations->jointLocations != nullptr &&
            locations->jointCount > indexTip
        ) {
            const XrHandJointLocationEXT& joint =
                locations->jointLocations[indexTip];
            flags = joint.locationFlags;
            valid =
                (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                (flags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            if (valid) {
                position = joint.pose.position;
            }
        }

        bool shouldLogReady = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gLatestRightIndexValid = valid;
            gLatestRightIndexFlags = flags;
            if (valid) {
                gLatestRightIndexTip = position;
                gLatestRightIndexBaseSpace = locateInfo->baseSpace;
                gLatestRightIndexTime = locateInfo->time;
                if (!gLoggedIndexTipTrackingReady) {
                    gLoggedIndexTipTrackingReady = true;
                    shouldLogReady = true;
                }
            }
        }

        if (shouldLogReady) {
            logLine("Right index-tip tracking ready for stereo rendering");
        }
    }

    bool shouldLog = callCount == 1 || (callCount != 0 && callCount % 300 == 0);

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        const auto iterator = gHandTrackerDispatch.find(handTracker);
        if (
            iterator != gHandTrackerDispatch.end() &&
            !iterator->second.loggedFirstSuccessfulLocate
        ) {
            iterator->second.loggedFirstSuccessfulLocate = true;
            shouldLog = true;
        }
    }

    if (shouldLog && locations != nullptr) {
        logLine(
            std::string("xrLocateHandJointsEXT sample: hand=") +
            handName(dispatch.hand) +
            ", active=" +
            (locations->isActive == XR_TRUE ? "true" : "false") +
            ", jointCount=" +
            std::to_string(locations->jointCount) +
            ", baseSpaceType=" +
            (
                locateInfo != nullptr
                    ? knownSpaceTypeName(locateInfo->baseSpace)
                    : "NO_LOCATE_INFO"
            ) +
            ", call=" +
            std::to_string(callCount)
        );

        const uint32_t indexTip =
            static_cast<uint32_t>(XR_HAND_JOINT_INDEX_TIP_EXT);

        if (
            locations->isActive == XR_TRUE &&
            locations->jointLocations != nullptr &&
            locations->jointCount > indexTip
        ) {
            const XrHandJointLocationEXT& joint =
                locations->jointLocations[indexTip];

            logLine(
                std::string("Index tip position: x=") +
                std::to_string(joint.pose.position.x) +
                ", y=" +
                std::to_string(joint.pose.position.y) +
                ", z=" +
                std::to_string(joint.pose.position.z) +
                ", flags=" +
                std::to_string(
                    static_cast<unsigned long long>(joint.locationFlags)
                )
            );
        }
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerLocateViews(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views
) {
    PFN_xrLocateViews nextLocateViews = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextLocateViews = gNextLocateViews;
    }

    if (nextLocateViews == nullptr) {
        logLine("xrLocateViews: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const XrResult result = nextLocateViews(
        session,
        viewLocateInfo,
        viewState,
        viewCapacityInput,
        viewCountOutput,
        views
    );

    if (
        XR_SUCCEEDED(result) &&
        viewState != nullptr &&
        viewCountOutput != nullptr &&
        views != nullptr &&
        viewCapacityInput >= 2 &&
        *viewCountOutput >= 2
    ) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gLatestViews[0] = views[0];
            gLatestViews[1] = views[1];
            gLatestViewCount = 2;
            gLatestViewStateFlags = viewState->viewStateFlags;
            gLatestViewSpace =
                viewLocateInfo != nullptr ? viewLocateInfo->space : XR_NULL_HANDLE;
            if (!gLoggedStereoViewSample) {
                gLoggedStereoViewSample = true;
                shouldLog = true;
            }
        }

        if (shouldLog) {
            logLine(
                std::string("Stereo views captured: count=2, flags=") +
                std::to_string(static_cast<unsigned long long>(viewState->viewStateFlags)) +
                ", leftX=" + std::to_string(views[0].pose.position.x) +
                ", rightX=" + std::to_string(views[1].pose.position.x)
            );
        }
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerEndFrame(
    XrSession session,
    const XrFrameEndInfo* frameEndInfo
) {
    PFN_xrEndFrame nextEndFrame = nullptr;
    uint64_t callCount = 0;

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextEndFrame = gNextEndFrame;
        callCount = ++gEndFrameCallCount;
    }

    if (nextEndFrame == nullptr) {
        logLine("xrEndFrame: downstream function unavailable");
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    if (callCount == 1 || callCount % 300 == 0) {
        const uint32_t layerCount =
            frameEndInfo != nullptr ? frameEndInfo->layerCount : 0;

        logLine(
            std::string("xrEndFrame sample: layerCount=") +
            std::to_string(layerCount) +
            ", call=" +
            std::to_string(callCount)
        );
    }

    return nextEndFrame(session, frameEndInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (name == nullptr || function == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    bool shouldLogRequest = true;

    if (std::strcmp(name, "xrLocateHandJointsEXT") == 0) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        if (gLoggedLocateHandJointsRequest) {
            shouldLogRequest = false;
        } else {
            gLoggedLocateHandJointsRequest = true;
        }
    }

    if (shouldLogRequest) {
        logLine(std::string("xrGetInstanceProcAddr requested: ") + name);
    }

    *function = nullptr;

    if (std::strcmp(name, "xrGetInstanceProcAddr") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerGetInstanceProcAddr);
        return XR_SUCCESS;
    }

    if (std::strcmp(name, "xrDestroyInstance") == 0 && instance != XR_NULL_HANDLE) {
        InstanceDispatch dispatch{};
        if (findInstanceDispatch(instance, &dispatch) && dispatch.destroyInstance != nullptr) {
            *function = reinterpret_cast<PFN_xrVoidFunction>(layerDestroyInstance);
            return XR_SUCCESS;
        }
    }

    PFN_xrGetInstanceProcAddr nextGetInstanceProcAddr = nullptr;

    InstanceDispatch dispatch{};
    if (findInstanceDispatch(instance, &dispatch)) {
        nextGetInstanceProcAddr = dispatch.getInstanceProcAddr;
    }

    // During instance creation, or for global commands queried with
    // XR_NULL_HANDLE, the per-instance table does not exist yet. Keep the
    // negotiated downstream function as a permissive fallback.
    if (nextGetInstanceProcAddr == nullptr) {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextGetInstanceProcAddr = gNextGetInstanceProcAddr;
    }

    if (nextGetInstanceProcAddr == nullptr) {
        logLine(std::string("xrGetInstanceProcAddr unavailable for: ") + name);
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    const bool isCreateHandTracker =
        std::strcmp(name, "xrCreateHandTrackerEXT") == 0;
    const bool isDestroyHandTracker =
        std::strcmp(name, "xrDestroyHandTrackerEXT") == 0;
    const bool isLocateHandJoints =
        std::strcmp(name, "xrLocateHandJointsEXT") == 0;
    const bool isEndFrame =
        std::strcmp(name, "xrEndFrame") == 0;
    const bool isLocateViews =
        std::strcmp(name, "xrLocateViews") == 0;
    const bool isCreateReferenceSpace =
        std::strcmp(name, "xrCreateReferenceSpace") == 0;
    const bool isDestroySpace =
        std::strcmp(name, "xrDestroySpace") == 0;
    const bool isCreateSession =
        std::strcmp(name, "xrCreateSession") == 0;
    const bool isCreateSwapchain =
        std::strcmp(name, "xrCreateSwapchain") == 0;
    const bool isDestroySwapchain =
        std::strcmp(name, "xrDestroySwapchain") == 0;
    const bool isEnumerateSwapchainImages =
        std::strcmp(name, "xrEnumerateSwapchainImages") == 0;
    const bool isAcquireSwapchainImage =
        std::strcmp(name, "xrAcquireSwapchainImage") == 0;
    const bool isWaitSwapchainImage =
        std::strcmp(name, "xrWaitSwapchainImage") == 0;
    const bool isReleaseSwapchainImage =
        std::strcmp(name, "xrReleaseSwapchainImage") == 0;

    const XrResult result =
        nextGetInstanceProcAddr(instance, name, function);

    if (XR_FAILED(result)) {
        logLine(
            std::string("xrGetInstanceProcAddr lookup failed for ") +
            name +
            ": " +
            std::to_string(static_cast<int>(result))
        );
        return result;
    }

    if (*function == nullptr) {
        return result;
    }

    if (isCreateHandTracker) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextCreateHandTracker =
                reinterpret_cast<PFN_xrCreateHandTrackerEXT>(*function);

            if (!gLoggedCreateHandTrackerIntercept) {
                gLoggedCreateHandTrackerIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerCreateHandTrackerEXT);

        if (shouldLog) {
            logLine("Intercepting xrCreateHandTrackerEXT");
        }
    } else if (isDestroyHandTracker) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextDestroyHandTracker =
                reinterpret_cast<PFN_xrDestroyHandTrackerEXT>(*function);

            if (!gLoggedDestroyHandTrackerIntercept) {
                gLoggedDestroyHandTrackerIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerDestroyHandTrackerEXT);

        if (shouldLog) {
            logLine("Intercepting xrDestroyHandTrackerEXT");
        }
    } else if (isLocateHandJoints) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextLocateHandJoints =
                reinterpret_cast<PFN_xrLocateHandJointsEXT>(*function);

            if (!gLoggedLocateHandJointsIntercept) {
                gLoggedLocateHandJointsIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerLocateHandJointsEXT);

        if (shouldLog) {
            logLine("Intercepting xrLocateHandJointsEXT");
        }
    } else if (isLocateViews) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextLocateViews = reinterpret_cast<PFN_xrLocateViews>(*function);

            if (!gLoggedLocateViewsIntercept) {
                gLoggedLocateViewsIntercept = true;
                shouldLog = true;
            }
        }

        *function = reinterpret_cast<PFN_xrVoidFunction>(layerLocateViews);

        if (shouldLog) {
            logLine("Intercepting xrLocateViews");
        }
    } else if (isEndFrame) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextEndFrame =
                reinterpret_cast<PFN_xrEndFrame>(*function);

            if (!gLoggedEndFrameIntercept) {
                gLoggedEndFrameIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerEndFrame);

        if (shouldLog) {
            logLine("Intercepting xrEndFrame");
        }
    } else if (isCreateReferenceSpace) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextCreateReferenceSpace =
                reinterpret_cast<PFN_xrCreateReferenceSpace>(*function);

            if (!gLoggedCreateReferenceSpaceIntercept) {
                gLoggedCreateReferenceSpaceIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerCreateReferenceSpace);

        if (shouldLog) {
            logLine("Intercepting xrCreateReferenceSpace");
        }
    } else if (isDestroySpace) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextDestroySpace =
                reinterpret_cast<PFN_xrDestroySpace>(*function);

            if (!gLoggedDestroySpaceIntercept) {
                gLoggedDestroySpaceIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerDestroySpace);

        if (shouldLog) {
            logLine("Intercepting xrDestroySpace");
        }
    } else if (isCreateSession) {
        bool shouldLog = false;

        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextCreateSession =
                reinterpret_cast<PFN_xrCreateSession>(*function);

            if (!gLoggedCreateSessionIntercept) {
                gLoggedCreateSessionIntercept = true;
                shouldLog = true;
            }
        }

        *function =
            reinterpret_cast<PFN_xrVoidFunction>(layerCreateSession);

        if (shouldLog) {
            logLine("Intercepting xrCreateSession");
        }
    } else if (isCreateSwapchain) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextCreateSwapchain = reinterpret_cast<PFN_xrCreateSwapchain>(*function);
            if (!gLoggedCreateSwapchainIntercept) {
                gLoggedCreateSwapchainIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerCreateSwapchain);
        if (shouldLog) logLine("Intercepting xrCreateSwapchain");
    } else if (isDestroySwapchain) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextDestroySwapchain = reinterpret_cast<PFN_xrDestroySwapchain>(*function);
            if (!gLoggedDestroySwapchainIntercept) {
                gLoggedDestroySwapchainIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerDestroySwapchain);
        if (shouldLog) logLine("Intercepting xrDestroySwapchain");
    } else if (isEnumerateSwapchainImages) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextEnumerateSwapchainImages = reinterpret_cast<PFN_xrEnumerateSwapchainImages>(*function);
            if (!gLoggedEnumerateSwapchainImagesIntercept) {
                gLoggedEnumerateSwapchainImagesIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerEnumerateSwapchainImages);
        if (shouldLog) logLine("Intercepting xrEnumerateSwapchainImages");
    } else if (isAcquireSwapchainImage) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextAcquireSwapchainImage = reinterpret_cast<PFN_xrAcquireSwapchainImage>(*function);
            if (!gLoggedAcquireSwapchainImageIntercept) {
                gLoggedAcquireSwapchainImageIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerAcquireSwapchainImage);
        if (shouldLog) logLine("Intercepting xrAcquireSwapchainImage");
    } else if (isWaitSwapchainImage) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextWaitSwapchainImage = reinterpret_cast<PFN_xrWaitSwapchainImage>(*function);
            if (!gLoggedWaitSwapchainImageIntercept) {
                gLoggedWaitSwapchainImageIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerWaitSwapchainImage);
        if (shouldLog) logLine("Intercepting xrWaitSwapchainImage");
    } else if (isReleaseSwapchainImage) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(gStateMutex);
            gNextReleaseSwapchainImage = reinterpret_cast<PFN_xrReleaseSwapchainImage>(*function);
            if (!gLoggedReleaseSwapchainImageIntercept) {
                gLoggedReleaseSwapchainImageIntercept = true;
                shouldLog = true;
            }
        }
        *function = reinterpret_cast<PFN_xrVoidFunction>(layerReleaseSwapchainImage);
        if (shouldLog) logLine("Intercepting xrReleaseSwapchainImage");
    }

    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL layerCreateApiLayerInstance(
    const XrInstanceCreateInfo* instanceCreateInfo,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* instance
) {
    if (
        instanceCreateInfo == nullptr ||
        apiLayerInfo == nullptr ||
        apiLayerInfo->nextInfo == nullptr ||
        instance == nullptr
    ) {
        logLine("layerCreateApiLayerInstance: invalid arguments");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    const XrApiLayerNextInfo* nextInfo = apiLayerInfo->nextInfo;

    if (
        nextInfo->nextGetInstanceProcAddr == nullptr ||
        nextInfo->nextCreateApiLayerInstance == nullptr
    ) {
        logLine("layerCreateApiLayerInstance: missing next-layer functions");
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        gNextGetInstanceProcAddr = nextInfo->nextGetInstanceProcAddr;
        gNextCreateApiLayerInstance = nextInfo->nextCreateApiLayerInstance;
    }

    XrApiLayerCreateInfo forwardedInfo = *apiLayerInfo;
    forwardedInfo.nextInfo = nextInfo->next;

    logLine("Forwarding OpenXR instance creation");

    const XrResult result = nextInfo->nextCreateApiLayerInstance(
        instanceCreateInfo,
        &forwardedInfo,
        instance
    );

    logLine(
        std::string("OpenXR instance creation result: ") +
        std::to_string(static_cast<int>(result))
    );

    if (XR_FAILED(result)) {
        return result;
    }

    InstanceDispatch dispatch{};
    dispatch.getInstanceProcAddr = nextInfo->nextGetInstanceProcAddr;

    PFN_xrVoidFunction destroyFunction = nullptr;
    const XrResult destroyLookupResult = dispatch.getInstanceProcAddr(
        *instance,
        "xrDestroyInstance",
        &destroyFunction
    );

    if (XR_SUCCEEDED(destroyLookupResult) && destroyFunction != nullptr) {
        dispatch.destroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(destroyFunction);
        storeInstanceDispatch(*instance, dispatch);
        logLine("Instance dispatch table created");
    } else {
        // Do not fail creation after the runtime has already created a valid
        // instance. Continue as a pass-through layer and record the anomaly.
        logLine(
            std::string("Warning: xrDestroyInstance lookup failed: ") +
            std::to_string(static_cast<int>(destroyLookupResult))
        );
    }

    return result;
}

}  // namespace

extern "C" {

__declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* apiLayerRequest
) {
    if (
        loaderInfo == nullptr ||
        layerName == nullptr ||
        apiLayerRequest == nullptr
    ) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (std::strcmp(layerName, kLayerName) != 0) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t interfaceVersion = std::min(
        loaderInfo->maxInterfaceVersion,
        static_cast<uint32_t>(XR_CURRENT_LOADER_API_LAYER_VERSION)
    );

    if (interfaceVersion < loaderInfo->minInterfaceVersion) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    apiLayerRequest->layerInterfaceVersion = interfaceVersion;
    apiLayerRequest->layerApiVersion = std::min(
        loaderInfo->maxApiVersion,
        static_cast<XrVersion>(XR_CURRENT_API_VERSION)
    );
    apiLayerRequest->getInstanceProcAddr = layerGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = layerCreateApiLayerInstance;

    logLine("SmartTouchXR: OpenXR loader negotiated the layer");

    return XR_SUCCESS;
}

}  // extern "C"
