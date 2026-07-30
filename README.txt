DCS HANDASSIST — HELLO LAYER v0.0.1
===================================

PURPOSE
-------
This is deliberately minimal.

It does not use hand tracking.
It does not hook xrWaitFrame.
It does not open UDP sockets.
It does not load any DCS Lua code.

It only:
1. negotiates with the OpenXR loader;
2. forwards instance creation unchanged;
3. writes a small log.

BUILD
-----
Use "Developer PowerShell for Visual Studio":

  cd "C:\DCS_HandAssist"
  Set-ExecutionPolicy -Scope Process Bypass
  .\build.ps1

INSTALL
-------
Close DCS and SteamVR, then:

  .\install-layer.ps1

TEST
----
1. Start Steam Link / SteamVR.
2. Start DCS in VR.
3. Confirm that DCS remains in VR.
4. Look for:

   %LOCALAPPDATA%\DCSHandAssist\HandAssist-layer.txt

Expected contents:

  Hello HandAssist: OpenXR loader negotiated the layer
  Forwarding OpenXR instance creation
  OpenXR instance creation result: 0

UNINSTALL / EMERGENCY DISABLE
-----------------------------
If DCS does not start in VR:

  .\uninstall-layer.ps1

You can also set this environment variable before launching DCS:

  DISABLE_DCS_HANDASSIST=1

IMPORTANT
---------
Keep the previous full HandAssist OpenXR layer uninstalled while testing this
minimal layer. The existing HandAssist Lua bridge may remain in place, but it
is not used by this Hello Layer.
