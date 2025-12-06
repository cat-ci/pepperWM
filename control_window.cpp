#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>

std::vector<Window> getAllWindows(Display* dpy) {
    Atom listAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    std::vector<Window> list;
    if (listAtom == None) return list;

    Atom type;
    int fmt;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), listAtom, 0, (~0L),
                           False, XA_WINDOW, &type, &fmt, &nItems, &bytesAfter,
                           &prop) == Success && prop) {
        Window* arr = reinterpret_cast<Window*>(prop);
        for (unsigned long i = 0; i < nItems; ++i)
            list.push_back(arr[i]);
        XFree(prop);
    }
    return list;
}

int getPID(Display* dpy, Window w) {
    Atom atomPID = XInternAtom(dpy, "_NET_WM_PID", True);
    if (atomPID == None) return -1;

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;
    int pid = -1;

    if (XGetWindowProperty(dpy, w, atomPID, 0, 1, False, XA_CARDINAL,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &prop) == Success &&
        prop) {
        if (nItems > 0)
            pid = *((unsigned long*)prop);
        XFree(prop);
    }
    return pid;
}

bool getFrameExtents(Display* dpy, Window w,
                     int& left, int& right, int& top, int& bottom) {
    Atom atom = XInternAtom(dpy, "_NET_FRAME_EXTENTS", True);
    if (atom == None) return false;

    Atom type;
    int fmt;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, w, atom, 0, 4, False, XA_CARDINAL, &type, &fmt,
                           &nItems, &bytesAfter, &prop) != Success || !prop)
        return false;

    if (nItems >= 4) {
        unsigned long* vals = reinterpret_cast<unsigned long*>(prop);
        left = vals[0];
        right = vals[1];
        top = vals[2];
        bottom = vals[3];
    }
    XFree(prop);
    return true;
}


void clearWindowStates(Display* dpy, Window w) {
    Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom maxH = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom maxV = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    auto removeState = [&](Atom stateAtom) {
        XEvent e;
        std::memset(&e, 0, sizeof(e));
        e.xclient.type = ClientMessage;
        e.xclient.message_type = wmState;
        e.xclient.display = dpy;
        e.xclient.window = w;
        e.xclient.format = 32;
        e.xclient.data.l[0] = 0;  
        e.xclient.data.l[1] = stateAtom;
        e.xclient.data.l[2] = 0;
        e.xclient.data.l[3] = 1;  
        e.xclient.data.l[4] = 0;

        XSendEvent(dpy, DefaultRootWindow(dpy), False,
                   SubstructureRedirectMask | SubstructureNotifyMask, &e);
    };

    removeState(fs);
    removeState(maxH);
    removeState(maxV);
    XFlush(dpy);
}

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
    e.xclient.data.l[0] =
        (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11);  
    e.xclient.data.l[1] = x;
    e.xclient.data.l[2] = y;
    e.xclient.data.l[3] = width;
    e.xclient.data.l[4] = height;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(dpy);
}


int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cerr << "Usage: " << argv[0]
                  << " <PID[@UID]> <X> <Y> <WIDTH> <HEIGHT>\n";
        return 1;
    }

    std::string arg = argv[1];
    int basePID = 0;
    int uid = 1;  

    size_t sep = arg.find('@');
    if (sep != std::string::npos) {
        basePID = std::stoi(arg.substr(0, sep));
        uid = std::stoi(arg.substr(sep + 1));
    } else {
        basePID = std::stoi(arg);
    }

    int x = std::atoi(argv[2]);
    int y = std::atoi(argv[3]);
    int width = std::atoi(argv[4]);
    int height = std::atoi(argv[5]);

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Error: cannot open X display.\n";
        return 2;
    }

    auto windows = getAllWindows(dpy);
    bool found = false;
    std::map<int, int> pidCounter;

    for (auto w : windows) {
        int pid = getPID(dpy, w);
        if (pid == -1) continue;
        pidCounter[pid]++;

        if (pid == basePID && pidCounter[pid] == uid) {
            found = true;

            clearWindowStates(dpy, w);

            int left = 0, right = 0, top = 0, bottom = 0;
            if (getFrameExtents(dpy, w, left, right, top, bottom)) {
                std::cout << "Frame extents: top=" << top << " left=" << left
                          << " right=" << right << " bottom=" << bottom << "\n";

                x += left;
                y += top;
                width -= (left + right);
                height -= (top + bottom);
            }

            sendMoveResize(dpy, w, x, y, width, height);
            break;
        }
    }

    if (!found)
        std::cerr << "No window found with ID " << arg << "\n";

    XCloseDisplay(dpy);
    return 0;
}