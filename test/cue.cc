module qsefs.cue;

import dxx.selftest;
import std;

namespace {

namespace test::qsefs::cue::load {

using dxx::selftest::UnitTest;

using namespace std::literals;

const UnitTest single_file = [] {
    static constexpr auto cue_txt = R"cue(
    )cue"sv;
}; // <-- single_file

} // <-- namespace test::qsefs::cue::load

} // <-- namespace <anonymous>
