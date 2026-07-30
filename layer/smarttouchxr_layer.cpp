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

struct HandTrackerDispatch {
    PFN_xrDestroyHandTrackerEXT destroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints = nullptr;
    XrHandEXT hand = XR_HAND_LEFT_EXT;
    uint64_t locateCallCount = 0;
    bool loggedFirstSuccessfulLocate = false;
};

std::mutex gStateMutex;
std::mutex gLogMutex;
PFN_xrGetInstanceProcAddr gNextGetInstanceProcAddr = nullptr;
PFN_xrCreateApiLayerInstance gNextCreateApiLayerInstance = nullptr;
PFN_xrCreateHandTrackerEXT gNextCreateHandTracker = nullptr;
PFN_xrDestroyHandTrackerEXT gNextDestroyHandTracker = nullptr;
PFN_xrLocateHandJointsEXT gNextLocateHandJoints = nullptr;
PFN_xrEndFrame gNextEndFrame = nullptr;
PFN_xrCreateReferenceSpace gNextCreateReferenceSpace = nullptr;
PFN_xrDestroySpace gNextDestroySpace = nullptr;
uint64_t gEndFrameCallCount = 0;
bool gLoggedCreateHandTrackerIntercept = false;
bool gLoggedDestroyHandTrackerIntercept = false;
bool gLoggedLocateHandJointsIntercept = false;
bool gLoggedEndFrameIntercept = false;
bool gLoggedCreateReferenceSpaceIntercept = false;
bool gLoggedDestroySpaceIntercept = false;
bool gLoggedLocateHandJointsRequest = false;
std::unordered_map<XrInstance, InstanceDispatch> gInstanceDispatch;
std::unordered_map<XrHandTrackerEXT, HandTrackerDispatch> gHandTrackerDispatch;
std::unordered_map<XrSpace, XrReferenceSpaceType> gReferenceSpaceTypes;

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
    const bool isCreateReferenceSpace =
        std::strcmp(name, "xrCreateReferenceSpace") == 0;
    const bool isDestroySpace =
        std::strcmp(name, "xrDestroySpace") == 0;

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
