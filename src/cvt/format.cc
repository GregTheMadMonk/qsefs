module;

#include <cmath> // prevent conflicts with `extern "C"` on GCC

extern "C" {
#include <libavutil/samplefmt.h>
} // <-- extern "C"

module qsefs.cvt;

import dxx.errors;
import dxx.utils;
import stl;

using namespace dxx::errors::literals;

namespace qsefs::cvt {

template <
    dxx::utils::FixedString t_name,
    bool t_planar,
    typename TSample
> struct Format : public FormatTraits {
    [[nodiscard]]
    virtual std::string_view name() const override
    { return t_name.view(); }

    [[nodiscard]]
    virtual bool is_ieee() const override
    { return std::floating_point<TSample>; }

    [[nodiscard]]
    virtual u16 bits_per_sample() const override
    { return sizeof(TSample) * 8; }

    [[nodiscard]]
    virtual bool is_packed() const override
    { return !t_planar; }
}; // <-- struct Format<t_name, TSample>

std::unique_ptr<FormatTraits> get_format(AVSampleFormat fmt) {
    switch (fmt) {
    case AV_SAMPLE_FMT_U8:
        return std::make_unique<Format<"unsigned 8-bit, packed", false, u8>>();
    case AV_SAMPLE_FMT_S16:
        return std::make_unique<Format<"signed 16-bit, packed", false, i16>>();
    case AV_SAMPLE_FMT_S32:
        return std::make_unique<Format<"signed 32-bit, packed", false, i32>>();
    case AV_SAMPLE_FMT_S64:
        return std::make_unique<Format<"signed 64-bit, packed", false, i64>>();
    case AV_SAMPLE_FMT_FLT:
        return std::make_unique<Format<"32-bit float, packed", false, f32>>();
    case AV_SAMPLE_FMT_DBL:
        return std::make_unique<Format<"64-bit float, packed", false, f64>>();
    case AV_SAMPLE_FMT_U8P:
        return std::make_unique<Format<"unsigned 8-bit, planar", true, u8>>();
    case AV_SAMPLE_FMT_S16P:
        return std::make_unique<Format<"signed 16-bit, planar", true, i16>>();
    case AV_SAMPLE_FMT_S32P:
        return std::make_unique<Format<"signed 32-bit, planar", true, i32>>();
    case AV_SAMPLE_FMT_S64P:
        return std::make_unique<Format<"signed 64-bit, planar", true, i64>>();
    case AV_SAMPLE_FMT_FLTP:
        return std::make_unique<Format<"32-bit float, planar", true, f32>>();
    case AV_SAMPLE_FMT_DBLP:
        return std::make_unique<Format<"64-bit float, planar", true, f64>>();
    default:
        throw "Unknown AVSampleFormat or AV_SAMPLE_FMT_NONE: {}"_err
              (std::to_underlying(fmt));
    }
} // <-- unique_ptr<FormatTraits> get_format(fmt)

} // <-- namespace qsefs::cvt
