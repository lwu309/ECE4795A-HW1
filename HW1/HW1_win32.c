#include <renderer.h>

#include <math.h>

#include <Windows.h>
#include <strsafe.h>

WCHAR errortext[64];
char modelname[MAX_PATH];
WORD modelnameindex;
const WCHAR classname[] = L"HW1";
const char openfilter[] = "RAW Triangle Files (*.raw)\0*.raw\0All Files (*.*)\0*.*\0\0";
const char opentitle[] = "Open a RAW triangle file";
const char rawext[] = "raw";
const char savefilter[] = "PNG (*.png)\0*.png\0\0";
const char savetitle[] = "Save to PNG file";
const char pngext[] = "png";
const WCHAR enabledtext[] = L"Enabled";
const WCHAR disabledtext[] = L"Disabled";
OPENFILENAMEA openfilename;
HWND mainwindow = NULL;
HWND loadtrianglesbutton = NULL;
HWND reloadconfigbutton = NULL;
HWND savetopngbutton = NULL;
HWND exitbutton = NULL;
HWND configurationslabel = NULL;
HWND materialdiffusereflectancelabel = NULL;
HBITMAP materialdiffusereflectanceblock = NULL;
const RECT materialdiffusereflectancerect = {744, 244, 758, 258};
const RECT materialdiffusereflectanceblockrect = {0, 0, 14, 14};
const RECT previewrect = {0, 0, 600, 600};
double previewrendertime = 0.0;
double outputrendertime = 0.0;
HBITMAP previewbitmap = NULL;
surface * previewsurface = NULL;
triangles rawtriangles = {0};
configurations configs;

LRESULT CALLBACK windowprocedure(HWND, UINT, WPARAM, LPARAM);
void WINAPI displayerrortext(HWND);
void WINAPI updatepreview(void);
void WINAPI updateconfigurationstext(void);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    char currentdirectory[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, currentdirectory);
    char filename[MAX_PATH];
    openfilename.lStructSize = sizeof(OPENFILENAMEW);
    openfilename.hwndOwner = NULL;
    openfilename.hInstance = hInstance;
    openfilename.lpstrFilter = NULL;
    openfilename.lpstrCustomFilter = NULL;
    openfilename.nMaxCustFilter = 0L;
    openfilename.nFilterIndex = 1L;
    openfilename.lpstrFile = filename;
    openfilename.nMaxFile = MAX_PATH;
    openfilename.lpstrFileTitle = NULL;
    openfilename.nMaxFileTitle = 0L;
    openfilename.lpstrInitialDir = currentdirectory;
    openfilename.lpstrTitle = NULL;
    openfilename.Flags = OFN_OVERWRITEPROMPT | OFN_LONGNAMES;
    openfilename.nFileOffset = 0;
    openfilename.nFileExtension = 0;
    openfilename.lpstrDefExt = NULL;
    openfilename.lCustData = (LPARAM)0;
    openfilename.lpfnHook = NULL;
    openfilename.lpTemplateName = NULL;
    openfilename.pvReserved = NULL;
    openfilename.dwReserved = 0L;
    openfilename.FlagsEx = 0L;

    WNDCLASSW windowclass;
    windowclass.style = CS_HREDRAW | CS_VREDRAW;
    windowclass.lpfnWndProc = windowprocedure;
    windowclass.cbClsExtra = 0;
    windowclass.cbWndExtra = 0;
    windowclass.hInstance = hInstance;
    windowclass.hIcon = NULL;
    windowclass.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    windowclass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    windowclass.lpszMenuName = NULL;
    windowclass.lpszClassName = classname;
    if (RegisterClassW(&windowclass) == 0) {
        return 0;
    }

    RECT clientarea = {0, 0, 800, 600};
    if (!AdjustWindowRect(&clientarea, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE)) {
        return 0;
    }

    int windowwidth = GetSystemMetrics(SM_CXSCREEN);
    int windowheight = GetSystemMetrics(SM_CYSCREEN);

    mainwindow = CreateWindowExW(0L, classname, L"Homework #1: DIY 3-D Rendering", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, (windowwidth - (clientarea.right - clientarea.left)) / 2, (windowheight - (clientarea.bottom - clientarea.top)) / 2, clientarea.right - clientarea.left, clientarea.bottom - clientarea.top, NULL, NULL, hInstance, NULL);
    if (mainwindow == NULL) {
        return 0;
    }

    HFONT MSSansSerif14 = CreateFontW(14, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Sans Serif");
    HFONT MSSansSerif20 = CreateFontW(20, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Sans Serif");

    loadtrianglesbutton = CreateWindowExW(0L, L"BUTTON", L"Load RAW triangles", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 615, 375, 170, 40, mainwindow, (HMENU)100, hInstance, NULL);
    if (loadtrianglesbutton == NULL) {
        return 0;
    }
    SendMessageW(loadtrianglesbutton, WM_SETFONT, (WPARAM)MSSansSerif20, TRUE);

    reloadconfigbutton = CreateWindowExW(0L, L"BUTTON", L"Reload configuration", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 615, 425, 170, 40, mainwindow, (HMENU)101, hInstance, NULL);
    if (reloadconfigbutton == NULL) {
        return 0;
    }
    SendMessageW(reloadconfigbutton, WM_SETFONT, (WPARAM)MSSansSerif20, TRUE);

    savetopngbutton = CreateWindowExW(0L, L"BUTTON", L"Save to PNG", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 615, 475, 170, 40, mainwindow, (HMENU)102, hInstance, NULL);
    if (savetopngbutton == NULL) {
        return 0;
    }
    SendMessageW(savetopngbutton, WM_SETFONT, (WPARAM)MSSansSerif20, TRUE);

    exitbutton = CreateWindowExW(0L, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 615, 525, 170, 40, mainwindow, (HMENU)103, hInstance, NULL);
    if (exitbutton == NULL) {
        return 0;
    }
    SendMessageW(exitbutton, WM_SETFONT, (WPARAM)MSSansSerif20, TRUE);

    configurationslabel = CreateWindowExW(0L, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 610, 10, 180, 234, mainwindow, (HMENU)104, hInstance, NULL);
    if (configurationslabel == NULL) {
        return 0;
    }
    SendMessageW(configurationslabel, WM_SETFONT, (WPARAM)MSSansSerif14, TRUE);

    materialdiffusereflectancelabel = CreateWindowExW(0L, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 610, 244, 130, 106, mainwindow, (HMENU)105, hInstance, NULL);
    if (materialdiffusereflectancelabel == NULL) {
        return 0;
    }
    SendMessageW(materialdiffusereflectancelabel, WM_SETFONT, (WPARAM)MSSansSerif14, TRUE);

    ShowWindow(mainwindow, SW_SHOWDEFAULT);
    UpdateWindow(mainwindow);

    MSG message;
    while (GetMessageW(&message, NULL, 0U, 0U)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DeleteObject(MSSansSerif20);
    DeleteObject(MSSansSerif14);

    return (int)message.wParam;
}

LRESULT CALLBACK windowprocedure(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC devicecontext = NULL;
    HDC memorycontext = NULL;
    HBRUSH materialdiffusereflectancebrush = NULL;
    PAINTSTRUCT paintstructure;
    DWORD starttime;
    switch (uMsg) {
    case WM_CREATE:
        timeBeginPeriod(1U);
        devicecontext = GetDC(hWnd);
        memorycontext = CreateCompatibleDC(devicecontext);
        previewbitmap = CreateCompatibleBitmap(devicecontext, 600, 600);
        materialdiffusereflectanceblock = CreateCompatibleBitmap(devicecontext, 14, 14);
        SelectObject(memorycontext, previewbitmap);
        FillRect(memorycontext, &previewrect, GetStockObject(BLACK_BRUSH));
        DeleteDC(memorycontext);
        ReleaseDC(hWnd, devicecontext);
        previewsurface = createsurface(600U, 600U);
        if (geterror() != RENDERER_ERROR_NONE) {
            displayerrortext(NULL);
            DestroyWindow(hWnd);
        }
        readconfigurations();
        if (geterror() != RENDERER_ERROR_NONE) {
            displayerrortext(NULL);
            DestroyWindow(hWnd);
        }
        getconfigurations(&configs);
        if (geterror() != RENDERER_ERROR_NONE) {
            displayerrortext(NULL);
            DestroyWindow(hWnd);
        }
        openfilename.hwndOwner = hWnd;
        return 0;
    case WM_DESTROY:
        releasetriangles(&rawtriangles);
        releasesurface(&previewsurface);
        DeleteObject(materialdiffusereflectanceblock);
        DeleteObject(previewbitmap);
        timeEndPeriod(1U);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        devicecontext = BeginPaint(hWnd, &paintstructure);
        memorycontext = CreateCompatibleDC(devicecontext);
        SelectObject(memorycontext, previewbitmap);
        BitBlt(devicecontext, 0, 0, 600, 600, memorycontext, 0, 0, SRCCOPY);
        SelectObject(memorycontext, materialdiffusereflectanceblock);
        materialdiffusereflectancebrush = CreateSolidBrush(RGB(configs.materialdiffusereflectancered, configs.materialdiffusereflectancegreen, configs.materialdiffusereflectanceblue));
        FillRect(memorycontext, &materialdiffusereflectanceblockrect, materialdiffusereflectancebrush);
        BitBlt(devicecontext, 744, 244, 14, 14, memorycontext, 0, 0, SRCCOPY);
        DeleteObject(materialdiffusereflectancebrush);
        DeleteDC(memorycontext);
        EndPaint(hWnd, &paintstructure);
        updateconfigurationstext();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 100:
            openfilename.lpstrFilter = openfilter;
            openfilename.lpstrFile[0] = '\0';
            openfilename.lpstrTitle = opentitle;
            openfilename.lpstrDefExt = rawext;
            if (GetOpenFileNameA(&openfilename)) {
                for (modelnameindex = 0; modelnameindex < openfilename.nFileExtension - openfilename.nFileOffset; modelnameindex += 1) {
                    modelname[modelnameindex] = openfilename.lpstrFile[openfilename.nFileOffset + modelnameindex];
                }
                releasetriangles(&rawtriangles);
                starttime = timeGetTime();
                loadrawtriangles(openfilename.lpstrFile, &rawtriangles);
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                rendersurface(&rawtriangles, previewsurface);
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                updatepreview();
                previewrendertime = (timeGetTime() - starttime) / 1000.0;
                InvalidateRect(hWnd, &previewrect, FALSE);
            }
            break;
        case 101:
            readconfigurations();
            if (geterror() != RENDERER_ERROR_NONE) {
                displayerrortext(NULL);
                DestroyWindow(hWnd);
            }
            getconfigurations(&configs);
            if (geterror() != RENDERER_ERROR_NONE) {
                displayerrortext(NULL);
                DestroyWindow(hWnd);
            }
            InvalidateRect(hWnd, &materialdiffusereflectancerect, FALSE);
            if (rawtriangles.size != 0) {
                starttime = timeGetTime();
                rendersurface(&rawtriangles, previewsurface);
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                updatepreview();
                previewrendertime = (timeGetTime() - starttime) / 1000.0;
                InvalidateRect(hWnd, &previewrect, FALSE);
            }
            break;
        case 102:
            if (rawtriangles.size == 0) {
                MessageBoxW(hWnd, L"Please load RAW triangles first!", L"Error", MB_ICONHAND | MB_OK);
                break;
            }
            openfilename.lpstrFilter = savefilter;
            CopyMemory(openfilename.lpstrFile, modelname, modelnameindex);
            openfilename.lpstrFile[modelnameindex] = 'p';
            openfilename.lpstrFile[modelnameindex + 1] = 'n';
            openfilename.lpstrFile[modelnameindex + 2] = 'g';
            openfilename.lpstrFile[modelnameindex + 3] = '\0';
            openfilename.lpstrTitle = savetitle;
            openfilename.lpstrDefExt = pngext;
            if (GetSaveFileNameA(&openfilename)) {
                starttime = timeGetTime();
                surface * rendertarget = createrendertarget();
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                rendersurface(&rawtriangles, rendertarget);
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                savesurfacetopngfile(rendertarget, openfilename.lpstrFile);
                if (geterror() != RENDERER_ERROR_NONE) {
                    displayerrortext(NULL);
                    DestroyWindow(hWnd);
                }
                outputrendertime = (timeGetTime() - starttime) / 1000.0;
                releasesurface(&rendertarget);
                updateconfigurationstext();
            }
            break;
        case 103:
            DestroyWindow(hWnd);
            break;
        default:
            break;
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

void WINAPI displayerrortext(HWND parentwindow)
{
    const char * errortextpointer = geterrortext(geterror());
    if (errortextpointer != NULL) {
        MultiByteToWideChar(CP_ACP, 0L, errortextpointer, -1, errortext, 64);
        MessageBoxW(parentwindow, errortext, L"Error", MB_ICONHAND | MB_OK);
    }
}

void WINAPI updatepreview(void)
{
    HDC devicecontext = GetDC(mainwindow);
    HDC memorycontext = CreateCompatibleDC(devicecontext);
    SelectObject(memorycontext, previewbitmap);
    FillRect(memorycontext, &previewrect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    for (uint16_t y = 0U; y < previewsurface->height; y += 1U) {
        for (uint16_t x = 0U; x < previewsurface->width; x += 1U) {
            float alpha = (float)(previewsurface->pixels[y * previewsurface->width + x] >> 24) / 255.F;
            unsigned int red = (unsigned int)roundf(alpha * (previewsurface->pixels[y * previewsurface->width + x] & 0xFFU));
            unsigned int green = (unsigned int)roundf(alpha * (previewsurface->pixels[y * previewsurface->width + x] >> 8 & 0xFFU));
            unsigned int blue = (unsigned int)roundf(alpha * (previewsurface->pixels[y * previewsurface->width + x] >> 16 & 0xFFU));
            SetPixel(memorycontext, x, y, RGB(red, green, blue));
        }
    }
    DeleteDC(memorycontext);
    ReleaseDC(mainwindow, devicecontext);
}

void WINAPI updateconfigurationstext(void)
{
    WCHAR buffer[1024];
    StringCchPrintfW(
        buffer, 1024,
        L"Light source position:\n"
        L"(%g, %g, %g)\n"
        L"Camera position:\n"
        L"(%g, %g, %g)\n"
        L"Camera look at point:\n"
        L"(%g, %g, %g)\n"
        L"Up vector:\n"
        L"(%g, %g, %g)\n"
        L"Object position:\n"
        L"(%g, %g, %g)\n"
        L"Object rotation:\n"
        L"X: %g°, Y: %g°, Z: %g°\n"
        L"Object scaling:\n"
        L"X: %g, Y: %g, Z: %g\n"
        L"Field of view: %g°\n"
        L"zNear: %g\n"
        L"zFar: %g\n"
        L"Output size: %u × %u\n",
        configs.lightsourcepositionx, configs.lightsourcepositiony, configs.lightsourcepositionz,
        configs.camerapositionx, configs.camerapositiony, configs.camerapositionz,
        configs.cameralookatpointx, configs.cameralookatpointy, configs.cameralookatpointz,
        configs.upvectorx, configs.upvectory, configs.upvectorz,
        configs.objectpositionx, configs.objectpositiony, configs.objectpositionz,
        configs.objectrotationx, configs.objectrotationy, configs.objectrotationz,
        configs.objectscalingx, configs.objectscalingy, configs.objectscalingz,
        configs.fieldofview,
        configs.znear,
        configs.zfar,
        configs.outputwidth, configs.outputheight
    );
    SetWindowTextW(configurationslabel, buffer);
    StringCchPrintfW(
        buffer, 1024,
        L"Material diffuse reflectance:\n"
        L"Backface culling: %s\n"
        L"Use z-buffer: %s\n"
        L"Preview render time:\n"
        L"%g s\n"
        L"Output render time:\n"
        L"%g s\n",
        configs.backfaceculling == 0 ? disabledtext : enabledtext,
        configs.usezbuffer == 0 ? disabledtext : enabledtext,
        previewrendertime,
        outputrendertime
    );
    SetWindowTextW(materialdiffusereflectancelabel, buffer);
}
