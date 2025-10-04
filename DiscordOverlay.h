#include <dwmapi.h>

void overlay() {
    HWND hwnd = FindWindowA("Chrome_WidgetWin_1", "Discord Overlay");
    if (!hwnd) {
        MessageBoxA(0, "Couldn't find Overly\n Please turn on all discord overlay!", "Error", MB_ICONERROR);
        return;
    }
    SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margin = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margin);
    ShowWindow(hwnd, SW_SHOW);
}