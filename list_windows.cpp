
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <json/json.h>

std::string getTextProperty(Display* dpy, Window win, const char* atomName) {
    Atom atom = XInternAtom(dpy, atomName, True);
    if (atom == None) return "";

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;
    std::string result;

    if (XGetWindowProperty(dpy, win, atom, 0, (~0L), False, AnyPropertyType,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &prop) == Success &&
        prop && nItems > 0) {
        result.assign(reinterpret_cast<char*>(prop),
                      reinterpret_cast<char*>(prop) + nItems);
    }
    if (prop)
        XFree(prop);
    return result;
}


bool isNormalWindow(Display* dpy, Window win) {
    Atom atomType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
    Atom atomNormal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    if (atomType == None) return false;

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;
    bool normal = false;

    if (XGetWindowProperty(dpy, win, atomType, 0, (~0L), False, XA_ATOM,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &prop) == Success &&
        prop) {
        Atom* atoms = reinterpret_cast<Atom*>(prop);
        for (unsigned long i = 0; i < nItems; ++i)
            if (atoms[i] == atomNormal)
                normal = true;
        XFree(prop);
    }
    return normal;
}


int getPID(Display* dpy, Window win) {
    Atom atomPID = XInternAtom(dpy, "_NET_WM_PID", True);
    if (atomPID == None) return -1;

    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;
    int pid = -1;

    if (XGetWindowProperty(dpy, win, atomPID, 0, 1, False, XA_CARDINAL,
                           &actualType, &actualFormat, &nItems, &bytesAfter,
                           &prop) == Success &&
        prop && nItems > 0) {
        pid = *((unsigned long*)prop);
        XFree(prop);
    }
    return pid;
}


std::string getProcName(int pid) {
    if (pid <= 0) return "(unknown)";
    std::string path = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream file(path);
    if (!file.good()) return "(unknown)";
    std::string name;
    std::getline(file, name);
    return name.empty() ? "(unknown)" : name;
}


void getGeometry(Display* dpy, Window win,
                 int& x, int& y, int& width, int& height) {
    Window root = DefaultRootWindow(dpy);
    XWindowAttributes attr;
    if (!XGetWindowAttributes(dpy, win, &attr)) {
        x = y = width = height = 0;
        return;
    }

    Window child;
    int absX = attr.x, absY = attr.y;
    XTranslateCoordinates(dpy, win, root, 0, 0, &absX, &absY, &child);
    x = absX;
    y = absY;
    width = attr.width;
    height = attr.height;
}


std::vector<Window> getClientList(Display* dpy) {
    std::vector<Window> list;
    Atom atomList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (atomList == None) return list;

    Atom type;
    int fmt;
    unsigned long nItems, bytesAfter;
    unsigned char* prop = nullptr;

    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), atomList, 0, (~0L),
                           False, XA_WINDOW, &type, &fmt, &nItems, &bytesAfter,
                           &prop) == Success &&
        prop) {
        Window* wins = reinterpret_cast<Window*>(prop);
        for (unsigned long i = 0; i < nItems; i++)
            list.push_back(wins[i]);
        XFree(prop);
    }
    return list;
}

int main() {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Cannot open X display\n";
        return 1;
    }

    std::vector<Window> windows = getClientList(dpy);
    std::map<int, int> pidCounts;

    Json::Value arr(Json::arrayValue);

    for (Window w : windows) {
        if (!isNormalWindow(dpy, w))
            continue;

        int pid = getPID(dpy, w);
        pidCounts[pid]++;

        std::string title = getTextProperty(dpy, w, "_NET_WM_NAME");
        if (title.empty())
            title = getTextProperty(dpy, w, "WM_NAME");
        if (title.empty())
            continue;

        int x = 0, y = 0, width = 0, height = 0;
        getGeometry(dpy, w, x, y, width, height);

        std::string proc = getProcName(pid);
        int uid = pidCounts[pid];
        std::string id = std::to_string(pid) + "@" + std::to_string(uid);

        Json::Value item;
        item["Title"] = title;
        item["PID"] = pid;
        item["UID"] = uid;
        item["ID"] = id;
        item["Process"] = proc;
        item["Width"] = width;
        item["Height"] = height;
        item["X"] = x;
        item["Y"] = y;

        arr.append(item);
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::cout << Json::writeString(builder, arr) << std::endl;

    XCloseDisplay(dpy);
    return 0;
}