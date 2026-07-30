#include <windows.h>

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

namespace {

constexpr const char* kLayerName = "XR_APILAYER_JBG_SmartTouchXR";

std::mutex gStateMutex;
std::mutex gLogMutex;
PFN_xrGetInstanceProcAddr gNextGetInstanceProcAddr = nullptr;
PFN_xrCreateApiLayerInstance gNextCreateApiLayerInstance = nullptr;

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
        *function = reinterpret_cast<PFN_xrVoidFunction>(
            layerGetInstanceProcAddr
        );
        return XR_SUCCESS;
    }

    PFN_xrGetInstanceProcAddr nextGetInstanceProcAddr = nullptr;
    {
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

    PFN_xrCreateApiLayerInstance nextCreateApiLayerInstance = nullptr;
    {
        std::lock_guard<std::mutex> lock(gStateMutex);
        nextCreateApiLayerInstance = gNextCreateApiLayerInstance;
    }

    const XrResult result = nextCreateApiLayerInstance(
        instanceCreateInfo,
        &forwardedInfo,
        instance
    );

    logLine(
        std::string("OpenXR instance creation result: ") +
        std::to_string(static_cast<int>(result))
    );

    return result;
}

}  // namespace

extern "C" {

XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
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
    apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr = layerGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = layerCreateApiLayerInstance;

    logLine("SmartTouchXR: OpenXR loader negotiated the layer");

    return XR_SUCCESS;
}

}  // extern "C"
