#pragma once

#include <utility>

// Equal-power-free, exactly-invertible mid/side matrix: decode(encode(l, r)) == (l, r)
// for every input, verified by DspTests. Used by EQBand to let each band choose which
// domain (Stereo/Mid/Side/Left/Right) it filters in, independently of the other bands.
namespace MidSide
{
    inline std::pair<float, float> encode (float l, float r) noexcept
    {
        return { (l + r) * 0.5f, (l - r) * 0.5f };
    }

    inline std::pair<float, float> decode (float m, float s) noexcept
    {
        return { m + s, m - s };
    }
}
