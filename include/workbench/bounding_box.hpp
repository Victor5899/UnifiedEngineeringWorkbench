#pragma once

namespace workbench {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BoundingBox {
    Vec3 min{};
    Vec3 max{};

    double dimension_x() const { return max.x - min.x; }
    double dimension_y() const { return max.y - min.y; }
    double dimension_z() const { return max.z - min.z; }
};

}
