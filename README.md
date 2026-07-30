# SmartTouchXR

SmartTouchXR is an experimental OpenXR API layer for natural cockpit interaction in VR flight simulators.

## Goal

Use OpenXR hand tracking to determine which cockpit control the user intends to interact with, while preserving the simulator's native:

- cockpit highlights;
- clickable actions;
- VR hands;
- interaction system.

## Initial target

- DCS World
- A-10C II
- SteamVR / OpenXR

## Development strategy

The project is developed incrementally:

1. Load a minimal Khronos-based OpenXR API layer.
2. Log instance creation.
3. Intercept session and frame calls.
4. Enable XR_EXT_hand_tracking.
5. Read the index fingertip position.
6. Add DCS communication.
7. Add hover, highlight and touch activation.

## Status

Early prototype.
