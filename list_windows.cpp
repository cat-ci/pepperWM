#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <jsoncpp/json/json.h>

std::string getTextProperty(Display *dpy, Window win, const char *atomName) {
    Atom atom = XInternAtom(dpy, atomName, True);
    if (atom == None)
        return "";
    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;

    int status = XGetWindowProperty(dpy, win, atom, 0, (~0L), False,
                                    AnyPropertyType, &actualType, &actualFormat,
                                    &nitems, &bytesAfter, &prop);
    std::string result;
    if (status == Success && prop && nitems > 0) {
        result.assign(reinterpret_cast<char *>(prop),
                      reinterpret_cast<char *>(prop) + nitems);
    }
    if (prop)
        XFree(prop);
    return result;
}

bool isNormalWindow(Display *dpy, Window win) {
    Atom atomType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
    Atom atomNormal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    if (atomType == None)
        return false;

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, win, atomType, 0, (~0L), False, XA_ATOM,
        &actualType, &actualFormat, &nitems, &bytesAfter,
        &prop) != Success) {
        return false;
        }

        bool normal = false;
    if (prop) {
        Atom *atoms = reinterpret_cast<Atom *>(prop);
        for (unsigned long i = 0; i < nitems; ++i) {
            if (atoms[i] == atomNormal) {
                normal = true;
                break;
            }
        }
        XFree(prop);
    }
    return normal;
}

int getPID(Display *dpy, Window win) {
    Atom atomPID = XInternAtom(dpy, "_NET_WM_PID", True);
    if (atomPID == None)
        return -1;

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, win, atomPID, 0, 1, False, XA_CARDINAL,
        &actualType, &actualFormat, &nitems, &bytesAfter,
        &prop) != Success ||
        !prop) {
        return -1;
        }

        int pid = (nitems > 0) ? *((unsigned long *)prop) : -1;
    XFree(prop);
    return pid;
}

std::string getProcessName(int pid) {
    if (pid <= 0)
        return "(unknown)";
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream f(path);
    if (!f)
        return "(unknown)";
    std::string name;
    std::getline(f, name);
    return name.empty() ? "(unknown)" : name;
}

struct WindowInfo {
    std::string title;
    int pid;
    std::string proc;
    int x, y, width, height;
};

std::vector<WindowInfo> listWindows(Display *dpy) {
    std::vector<WindowInfo> out;
    Atom atomClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (atomClientList == None)
        return out;

    Atom actualType;
    int actualFormat;
    unsigned long nitems, bytesAfter;
    unsigned char *prop = nullptr;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), atomClientList, 0,
        (~0L), False, XA_WINDOW, &actualType, &actualFormat,
                           &nitems, &bytesAfter, &prop) != Success ||
                           !prop) {
        return out;
                           }

                           Window *wins = reinterpret_cast<Window *>(prop);

                           for (unsigned long i = 0; i < nitems; ++i) {
                               Window w = wins[i];
                               if (!isNormalWindow(dpy, w))
                                   continue;

                               std::string title = getTextProperty(dpy, w, "_NET_WM_NAME");
                               if (title.empty())
                                   title = getTextProperty(dpy, w, "WM_NAME");
                               if (title.empty())
                                   continue;

                               XWindowAttributes attr;
                               if (!XGetWindowAttributes(dpy, w, &attr))
                                   continue;

                               int absX = attr.x;
                               int absY = attr.y;
                               Window child;
                               XTranslateCoordinates(dpy, w, DefaultRootWindow(dpy), 0, 0, &absX,
                                                     &absY, &child);

                               int pid = getPID(dpy, w);
                               std::string proc = getProcessName(pid);

                               out.push_back({title, pid, proc, absX, absY, attr.width, attr.height});
                           }

                           XFree(prop);
                           return out;
}


int main() {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Cannot open X display\n";
        return 1;
    }

    auto windows = listWindows(dpy);
    XCloseDisplay(dpy);

    Json::Value arr(Json::arrayValue);
    for (const auto &w : windows) {
        Json::Value item;
        // --- Info ---
        item["Title"] = w.title;
        item["PID"] = w.pid;
        item["Process"] = w.proc;
        // --- Dimensions ---
        item["Width"] = w.width;
        item["Height"] = w.height;
        // --- Position ---
        item["X"] = w.x;
        item["Y"] = w.y;

        arr.append(item);
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::cout << Json::writeString(builder, arr) << std::endl;
    return 0;
}
