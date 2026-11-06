# Applied to JUCE by FetchContent's PATCH_COMMAND (run with -DJUCE_DIR=<juce source dir>).
#
# JUCE 8 creates every Windows plugin window with the Direct2D renderer, and it builds the
# Direct2D swapchain inside the window's constructor. Under Wine/Proton (e.g. FL Studio in
# Lutris) that swapchain never shows anything, and once it has been attached to the window,
# switching to the software renderer later doesn't help - the window stays black.
#
# This patch makes JUCE pick the software (GDI) renderer at window creation when running
# under Wine, detected via ntdll's wine_get_version export. Real Windows keeps Direct2D.

set(file "${JUCE_DIR}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
file(READ "${file}" src)

set(marker "SPECTRASHELL_WINE_PATCH")
string(FIND "${src}" "${marker}" already)
if(NOT already EQUAL -1)
    return()
endif()

set(old "return new HWNDComponentPeer { *this, styleFlags, (HWND) parentHWND, false, 1 };")
set(new "/* ${marker} */\n    const int engine = GetProcAddress (GetModuleHandleW (L\"ntdll.dll\"), \"wine_get_version\") != nullptr ? 0 : 1;\n    return new HWNDComponentPeer { *this, styleFlags, (HWND) parentHWND, false, engine };")

string(FIND "${src}" "${old}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "PatchJuceWine: expected code not found in ${file}. "
                        "JUCE changed - update cmake/PatchJuceWine.cmake for this JUCE version.")
endif()

string(REPLACE "${old}" "${new}" src "${src}")
file(WRITE "${file}" "${src}")
message(STATUS "PatchJuceWine: JUCE uses the software renderer under Wine")
