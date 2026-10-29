#pragma once

#include "layout.h"

struct KeyData {
    const char *id;
    quint8 hid;
    Region region;
    int x, y;
    double width;
};

extern const KeyData kKeyData[];
extern const int kKeyDataCount;
extern const quint8 kRegionHids[4][42];
extern const int kAquaPrefix[4];
