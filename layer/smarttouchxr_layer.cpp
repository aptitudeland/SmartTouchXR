#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

constexpr const char* kLayerName = "XR_APILAYER_JBG_SmartTouchXR";

struct InstanceDispatch {
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance destroyInstance = nullptr;
};

std::mutex gStateMutex;
std::mutex gLogMutex;
PFN_xrGetInstanceProcAddr gNextGetInstanceProcAddr = nullptr;
PFN_xrCreateApiLayerInstance gNextCreateApiLayerInstance = nullptr;
std::unordered_map<XrInstance, InstanceDispatch> gInstanceDispatch;

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

XRAPI_ATTR XrResult XRAPI_CALL layerGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function
) {
    if (name == nullptr || function == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
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
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    return nextGetInstanceProcAddr(instance, name, function);
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
