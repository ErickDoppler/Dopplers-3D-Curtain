// The tool bar: dark strip with rounded buttons and vector icons drawn in code (same look as SFX-Parser), with a hint on hover.
#pragma once
#include <windows.h>

HWND createToolbar(HWND parent, HINSTANCE inst);
int toolbarHeight();
void toolbarEnable(int id, bool on);
void toolbarCheck(int id, bool on);          // a tool button that is switched on is drawn green
