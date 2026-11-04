#pragma once

#include "layout.h"

struct KeyData {
    const char *id;
    quint8 hid;
    Region region;
    double x, y; // Centre, photo pixels, y downwards.
    int w, h;    // Size, photo pixels.
    bool visible;
    int cx, cy;  // Position in the controller's wave units, -1 when GG has none.
};

extern const KeyData kKeyData[];
extern const int kKeyDataCount;
extern const quint8 kRegionHids[4][42];
extern const int kAquaPrefix[4];
