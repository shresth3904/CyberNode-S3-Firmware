#ifndef CYBERNODE_NAVIGATION_GEOMETRY_H
#define CYBERNODE_NAVIGATION_GEOMETRY_H



namespace CyberNodeNavigation {

static const int MAP_MIN_X = 0;
static const int MAP_MAX_X = 127;
static const int MAP_MIN_Y = 10;
static const int MAP_MAX_Y = 63;

inline int roundPixel(double value) {
    return static_cast<int>(value >= 0.0 ? value + 0.5 : value - 0.5);
}





inline bool clipLineToViewport(int& x0, int& y0, int& x1, int& y1) {
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    double tEnter = 0.0;
    double tExit = 1.0;

    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {
        static_cast<double>(x0 - MAP_MIN_X),
        static_cast<double>(MAP_MAX_X - x0),
        static_cast<double>(y0 - MAP_MIN_Y),
        static_cast<double>(MAP_MAX_Y - y0)
    };

    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false; 
            continue;
        }
        const double ratio = q[i] / p[i];
        if (p[i] < 0.0) {
            if (ratio > tExit) return false;
            if (ratio > tEnter) tEnter = ratio;
        } else {
            if (ratio < tEnter) return false;
            if (ratio < tExit) tExit = ratio;
        }
    }

    if (tEnter > tExit) return false;

    const int originalX = x0;
    const int originalY = y0;
    x0 = roundPixel(static_cast<double>(originalX) + tEnter * dx);
    y0 = roundPixel(static_cast<double>(originalY) + tEnter * dy);
    x1 = roundPixel(static_cast<double>(originalX) + tExit * dx);
    y1 = roundPixel(static_cast<double>(originalY) + tExit * dy);

    if (x0 < MAP_MIN_X) x0 = MAP_MIN_X;
    if (x0 > MAP_MAX_X) x0 = MAP_MAX_X;
    if (x1 < MAP_MIN_X) x1 = MAP_MIN_X;
    if (x1 > MAP_MAX_X) x1 = MAP_MAX_X;
    if (y0 < MAP_MIN_Y) y0 = MAP_MIN_Y;
    if (y0 > MAP_MAX_Y) y0 = MAP_MAX_Y;
    if (y1 < MAP_MIN_Y) y1 = MAP_MIN_Y;
    if (y1 > MAP_MAX_Y) y1 = MAP_MAX_Y;
    return true;
}

} 

#endif 
