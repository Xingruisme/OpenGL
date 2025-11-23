#include <windows.h>
#include <gl/GL.h>
#include "SilkSimulation.h"
#include <chrono>

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

static bool SetupPixelFormat(HDC hdc)
{
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (pf == 0) return false;
    return SetPixelFormat(hdc, pf, &pfd) != FALSE;
}

int main()
{
    // register class
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SilkSimWindowClass";
    RegisterClassA(&wc);

    // create window
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Silk Simulation (Win32 OpenGL)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return -1;

    HDC hdc = GetDC(hwnd);
    if (!SetupPixelFormat(hdc)) return -1;

    HGLRC glrc = wglCreateContext(hdc);
    if (!glrc) return -1;
    wglMakeCurrent(hdc, glrc);

    // basic GL state
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    SilkSimulation sim;
    sim.initialize();

    auto last = std::chrono::high_resolution_clock::now();
    bool running = true;
    while (running) {
        // process messages
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        // time step
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> elapsed = now - last;
        last = now;
        float dt = elapsed.count();
        if (dt > 0.033f) dt = 0.033f; // clamp

        // viewport
        RECT r; GetClientRect(hwnd, &r);
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        glViewport(0, 0, w, h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        // keep aspect: map [-1,1]x[-1,1] to window
        float aspect = (float)w / (float)h;
        if (aspect >= 1.0f) {
            glOrtho(-aspect, aspect, -1.0, 1.0, -1.0, 1.0);
        } else {
            glOrtho(-1.0, 1.0, -1.0f / aspect, 1.0f / aspect, -1.0, 1.0);
        }
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        sim.step(dt);
        sim.render();

        SwapBuffers(hdc);
        // small sleep to avoid 100% CPU
        Sleep(1);
    }

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 0;
}
