#include <vec_tools/fill.h>

Vec VecTools::fill(Scalar alpha, Vec x) {
    x.data().fill_(alpha);
    return x;
}

Vec VecTools::makeSequence(Scalar from, Scalar step, Vec x) {
    const float to = static_cast<float>(from + step * (x.dim() - 1));
    at::range_out(x.data(), (float) from, to, (float) step);
    return x;
}
