module qsefs.cvt;

import dxx.errors;
import dxx.overload;

using namespace dxx::errors::literals;

namespace qsefs::cvt {

/**
 * @brief Write track metadata section of the WAV file
 */
static std::vector<char> write_metadata(const Metadata& meta) {
    std::vector<char> ret{};

    static constexpr auto ctx  = stdm::access_context::current();
    static constexpr auto flds = std::define_static_array(
        stdm::nonstatic_data_members_of(^^Metadata, ctx)
    );

    template for (constexpr auto& fld : flds) {
        static constexpr auto anns = std::define_static_array(
            stdm::annotations_of_with_type(fld, ^^WavTag)
        );

        template for (constexpr auto ann : anns) {
            const auto tag = stdm::extract<WavTag>(ann);

            const auto& field = meta.[:fld:];
            using FieldType = std::remove_cvref_t<decltype(field)>;

            decltype(auto) str = [&meta] -> decltype(auto) {
                if constexpr (std::same_as<FieldType, std::string>) {
                    return meta.[:fld:];
                } else {
                    return std::to_string(meta.[:fld:]);
                }
            } ();

            if (str.empty()) {
                continue;
            }

            for (char c : tag.tag) {
                ret.push_back(c);
            }

            const auto sz = std::bit_cast<std::array<char, 4>>(
                static_cast<u32>(str.size())
            );
            for (char c : sz) {
                ret.push_back(c);
            }

            for (char c : str) {
                ret.push_back(c);
            }

            if (ret.size() % 2 == 1) {
                // Padding to even bytes
                ret.emplace_back();
            }
        }
    }

    return ret;
} // <-- std::vector<char> write_metadata(writer)

std::vector<u8> WAVHeader::pack(u32 data_size, const Metadata& meta) const {
    std::vector<u8> ret{};

    const auto metadata = write_metadata(meta);
    const u32 metadata_size = metadata.size() + 4 * 3;

    static_assert(riff_size + fmt_size + 8 == 44);
    // Calculate the header size
    ret.resize(riff_size + fmt_size + 8 + metadata_size, 0);
    uz cur = 0; // "cursor"

    const auto write = dxx::overload::Overload{
        [&ret, &cur] (u8 byte) {
            ret.at(cur++) = byte;
        },
        [] <uz n> (this auto& self, const char (&str)[n]) {
            for (uz i = 0; i < n - 1; ++i) {
                self(str[i]);
            }
        },
        [] <typename T> (this auto& self, const T& v) {
            const auto buf = std::bit_cast<std::array<u8, sizeof(T)>>(v);

            for (u8 byte : buf) {
                self(byte);
            }
        },
    }; // <-- write(v)

    // riff chunk id
    write("RIFF");
    // riff chunk size
    write(u32{ riff_size - (2 * 4) + fmt_size + data_size + metadata_size });
    // wave id
    write("WAVE");

    write("LIST");
    write(metadata_size - 8);
    write("INFO");
    for (char c : metadata) {
        write(c);
    }

    // fmt chunk id
    write("fmt ");
    // fmt chunk size
    write(static_cast<u32>(fmt_size - 2 * 4));
    // format tag
    write(std::to_underlying(this->tag));

    write(this->channels);
    write(this->sample_rate);

    if (this->bits_per_sample % 8 != 0) {
        throw "Bits per sample that's not divisible by 8 is unsupported: {}"_err
              (this->bits_per_sample);
    }

    write(
        static_cast<u32>(
            this->sample_rate
            * (this->bits_per_sample / 8)
            * this->channels
        )
    ); // byte rate

    write(
        static_cast<u16>(this->channels * this->bits_per_sample / 8)
    ); // alignment

    write(this->bits_per_sample);

    write("data");
    write(data_size);

    return ret;
} // <-- vector<u8> WAVHeader::pack(data_size, meta) const

u32 WAVHeader::data_size_for(const Duration& dur) const {
    const auto samples = dur.count() * this->sample_rate
                       * Duration::period::num / Duration::period::den;

    return samples * this->bits_per_sample * this->channels / 8;
} // <-- u32 WAVHeader::data_size_for(dur) const

} // <-- namespace qsefs::cvt
