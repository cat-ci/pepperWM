// control_window.cpp
// Usage: ./control_window <PID> <X> <Y> <WIDTH> <HEIGHT>
// Moves/resizes window safely (handles SSD/CSD + removes fullscreen/maximize)
//
// Build: g++ -O2 control_window.cpp -o control_window -lX11 -lXmu

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <cstdlib>

// ------------------------------------------------------
// Enumerate all windows from root
// ------------------------------------------------------
std::vector<Window> getAllWindows(Display* dpy) {
    std::vector<Window> wins;
    Atom clientList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    Atom type;
    int fmt;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), clientList, 0, (~0L),
        False, XA_WINDOW, &type, &fmt, &nitems, &bytesAfter,
        &prop) == Success && prop) {
        Window* arr = reinterpret_cast<Window*>(prop);
    for (unsigned long i = 0; i < nitems; ++i)
        wins.push_back(arr[i]);
        XFree(prop);
        }
        return wins;
}

// ------------------------------------------------------
// Get PID for window
// ------------------------------------------------------
int getPID(Display* dpy, Window w) {
    Atom atom = XInternAtom(dpy, "_NET_WM_PID", True);
    Atom type;
    int fmt;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;
    int pid = -1;

    if (XGetWindowProperty(dpy, w, atom, 0, 1, False, XA_CARDINAL, &type, &fmt,
        &nitems, &bytesAfter, &prop) == Success &&
        prop) {
        if (nitems > 0)
            pid = *((unsigned long*)prop);
        XFree(prop);
        }
        return pid;
}

// ------------------------------------------------------
// _NET_FRAME_EXTENTS for SSD adjustments
// ------------------------------------------------------
bool getFrameExtents(Display* dpy, Window w,
                     int& left, int& right, int& top, int& bottom) {
    Atom atom = XInternAtom(dpy, "_NET_FRAME_EXTENTS", True);
    if (atom == None) return false;
    Atom type;
    int fmt;
    unsigned long nitems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, w, atom, 0, 4, False, XA_CARDINAL, &type, &fmt,
        &nitems, &bytesAfter, &prop) != Success || !prop)
        return false;

    if (nitems >= 4) {
        unsigned long* vals = reinterpret_cast<unsigned long*>(prop);
        left = vals[0];
        right = vals[1];
        top = vals[2];
        bottom = vals[3];
    }
    XFree(prop);
    return true;
                     }

                     // ------------------------------------------------------
                     // Remove fullscreen / maximized states before resizing
                     // ------------------------------------------------------
                     void clearWindowStates(Display* dpy, Window w) {
                         Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
                         Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
                         Atom maxH = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
                         Atom maxV = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

                         // Prepare client message to toggle off those states
                         auto sendState = [&](Atom stateAtom) {
                             XEvent e;
                             std::memset(&e, 0, sizeof(e));
                             e.xclient.type = ClientMessage;
                             e.xclient.message_type = wmState;
                             e.xclient.display = dpy;
                             e.xclient.window = w;
                             e.xclient.format = 32;
                             e.xclient.data.l[0] = 0;  // _NET_WM_STATE_REMOVE
                             e.xclient.data.l[1] = stateAtom;
                             e.xclient.data.l[2] = 0;
                             e.xclient.data.l[3] = 1;  // Normal source
                             e.xclient.data.l[4] = 0;

                             XSendEvent(dpy, DefaultRootWindow(dpy), False,
                                        SubstructureRedirectMask | SubstructureNotifyMask, &e);
                         };

                         sendState(fs);
                         sendState(maxH);
                         sendState(maxV);
                         XFlush(dpy);
                     }

                     // ------------------------------------------------------
                     // Request move & resize
                     // ------------------------------------------------------
                     void sendMoveResize(Display* dpy, Window w,
                                         int x, int y, int width, int height) {
                         Atom moveresize = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", False);
                         if (moveresize == None) {
                             XMoveResizeWindow(dpy, w, x, y, width, height);
                             XFlush(dpy);
                             return;
                         }

                         XEvent e;
                         std::memset(&e, 0, sizeof(e));
                         e.xclient.type = ClientMessage;
                         e.xclient.send_event = True;
                         e.xclient.display = dpy;
                         e.xclient.window = w;
                         e.xclient.message_type = moveresize;
                         e.xclient.format = 32;
                         e.xclient.data.l[0] = (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);
                         e.xclient.data.l[1] = x;
                         e.xclient.data.l[2] = y;
                         e.xclient.data.l[3] = width;
                         e.xclient.data.l[4] = height;

                         XSendEvent(dpy, DefaultRootWindow(dpy), False,
                                    SubstructureRedirectMask | SubstructureNotifyMask, &e);
                         XFlush(dpy);
                                         }

                                         // ------------------------------------------------------
                                         // Main
                                         // ------------------------------------------------------
                                         int main(int argc, char* argv[]) {
                                             if (argc != 6) {
                                                 std::cerr << "Usage: " << argv[0]
                                                 << " <PID> <X> <Y> <WIDTH> <HEIGHT>\n";
                                                 return 1;
                                             }

                                             int targetPID = std::atoi(argv[1]);
                                             int x = std::atoi(argv[2]);
                                             int y = std::atoi(argv[3]);
                                             int width = std::atoi(argv[4]);
                                             int height = std::atoi(argv[5]);

                                             Display* dpy = XOpenDisplay(nullptr);
                                             if (!dpy) {
                                                 std::cerr << "Cannot open X display\n";
                                                 return 2;
                                             }

                                             auto windows = getAllWindows(dpy);
                                             bool found = false;

                                             for (auto w : windows) {
                                                 int pid = getPID(dpy, w);
                                                 if (pid == targetPID) {
                                                     found = true;
                                                     clearWindowStates(dpy, w);  // 🧠 Important: remove fullscreen/maximized first

                                                     int left = 0, right = 0, top = 0, bottom = 0;
                                                     if (getFrameExtents(dpy, w, left, right, top, bottom)) {
                                                         std::cout << "Frame extents: L=" << left << " R=" << right
                                                         << " T=" << top << " B=" << bottom << "\n";

                                                         // Adjust to match outer frame
                                                         x += left;
                                                         y += top;
                                                         width -= (left + right);
                                                         height -= (top + bottom);
                                                     }

                                                     sendMoveResize(dpy, w, x, y, width, height);
                                                 }
                                             }

                                             if (!found)
                                                 std::cerr << "No window found with PID " << targetPID << "\n";

                                             XCloseDisplay(dpy);
                                             return 0;
                                         }
