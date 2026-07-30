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
    LONG height
) {
    if (rectangles == nullptr) {
        return;
    }

    constexpr int kSegments = 18;
    constexpr LONG kHalfThickness = 4;
    for (int segment = 0; segment <= kSegments; ++segment) {
        const float t = static_cast<float>(segment) / static_cast<float>(kSegments);
        const LONG x = static_cast<LONG>(x0 + (x1 - x0) * t);
        const LONG y = static_cast<LONG>(y0 + (y1 - y0) * t);
        D3D11_RECT rectangle{
            std::max<LONG>(0, x - kHalfThickness),
            std::max<LONG>(0, y - kHalfThickness),
            std::min<LONG>(width, x + kHalfThickness + 1),
            std::min<LONG>(height, y + kHalfThickness + 1)
        };
        if (rectangle.right > rectangle.left && rectangle.bottom > rectangle.top) {
            rectangles->push_back(rectangle);
        }
    }
}

void drawTrackedIndexTipCube(XrSwapchain swapchain, int64_t imageIndex) {
    ID3D11RenderTargetView* renderTargetView = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t eyeIndex = 0;
    XrView eyeView{XR_TYPE_VIEW};
    XrView leftView{XR_TYPE_VIEW};
    XrView rightView{XR_TYPE_VIEW};
    XrVector3f rightIndexTip{0.0f, 0.0f, 0.0f};

    {
        std::lock_guard<std::mutex> lock(gStateMutex);
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

        renderTargetView->AddRef();
        width = it->second.createInfo.width;
        height = it->second.createInfo.height;
        eyeIndex = std::min<uint32_t>(it->second.eyeIndex, 1);
        eyeView = gLatestViews[eyeIndex];
        leftView = gLatestViews[0];
        rightView = gLatestViews[1];
        rightIndexTip = gLatestRightIndexTip;
    }

    // Use the current head orientation only to orient the small wireframe.
    // Its center comes directly from the tracked right index fingertip.
    const Vec3 forward = rotateByQuaternion(leftView.pose.orientation, {0.0f, 0.0f, -1.0f});
    const Vec3 right = rotateByQuaternion(leftView.pose.orientation, {1.0f, 0.0f, 0.0f});
    const Vec3 up = rotateByQuaternion(leftView.pose.orientation, {0.0f, 1.0f, 0.0f});

    // The hand joint and the eye views are both expressed in the exact same
    // OpenXR base space. This lets us project the fingertip directly into each
    // eye without an additional coordinate conversion.
    const Vec3 cubeCenter{
        rightIndexTip.x,
        rightIndexTip.y,
        rightIndexTip.z
    };
    constexpr float kHalfSize = 0.0125f;
    std::array<Vec3, 8> corners{};
    size_t cornerIndex = 0;
    for (int z = -1; z <= 1; z += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                corners[cornerIndex++] = add(
                    cubeCenter,
                    add(
                        scale(right, static_cast<float>(x) * kHalfSize),
                        add(
                            scale(up, static_cast<float>(y) * kHalfSize),
                            scale(forward, static_cast<float>(z) * kHalfSize)
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

    std::vector<D3D11_RECT> rectangles;
    rectangles.reserve(kEdges.size() * 19);
    for (const auto& edge : kEdges) {
        if (!projected[edge[0]] || !projected[edge[1]]) {
            continue;
        }
        appendLineRectangles(
            &rectangles,
            pixelX[edge[0]], pixelY[edge[0]],
            pixelX[edge[1]], pixelY[edge[1]],
            static_cast<LONG>(width), static_cast<LONG>(height)
        );
    }

    if (rectangles.empty()) {
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

    const FLOAT color[4] = {0.1f, 1.0f, 0.25f, 1.0f};
    context1->ClearView(
        renderTargetView,
        color,
        rectangles.data(),
        static_cast<UINT>(rectangles.size())
    );

    context1->Release();
    renderTargetView->Release();

    bool shouldLog = false;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        if (!gLoggedIndexTipCubeDraw) {
            gLoggedIndexTipCubeDraw = true;
            shouldLog = true;
        }
    }
    if (shouldLog) {
        logLine("Stereo-projected right index-tip cube draw submitted successfully");
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

    drawTrackedIndexTipCube(swapchain, lastIndex);

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
