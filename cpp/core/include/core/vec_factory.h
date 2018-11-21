#pragma once

#include "vec.h"

enum class VecType {
    Cpu
};

class VecFactory {
public:
    static Vec create(VecType type, int64_t dim);
};
