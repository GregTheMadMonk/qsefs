module qsefs.cue;

import dxx.selftest;
import stl;

namespace {

namespace test::qsefs::cue::load {

using dxx::selftest::UnitTest;
using dxx::selftest::test;
using dxx::selftest::check;

using namespace std::literals;

using ::qsefs::Cue;

namespace single_file {

const UnitTest simple = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 01 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;
    const auto tracks = Cue::parse(cue_txt, unknown_tags);

    check(tracks.size()) == 3uz;
    test(unknown_tags.empty());

    for (auto& t : tracks) {
        check(t.meta.artist)  == "Monk";
        check(t.meta.year)    == 2073;
        check(t.meta.comment) == "Test data";
        check(t.meta.disc_id) == "12345678";
        check(t.meta.album)   == "Among Us";
        check(t.file)         == "Among Us.flac";
    }

    check(tracks[0].meta.title) == "Sus";
    check(tracks[0].start)      == Cue::Frames::zero();
    check(tracks[0].end)        == stdc::minutes{3} + stdc::seconds{39};

    check(tracks[1].meta.title) == "Impostor";
    check(tracks[1].start)      == tracks[0].end;
    check(tracks[1].end)        == stdc::minutes{5}
                                 + stdc::seconds{50}
                                 + Cue::Frames{15};

    check(tracks[2].meta.title) == "Electrical (Instrumental)";
    check(tracks[2].start)      == tracks[1].end;
    check(tracks[2].end)        == Cue::Frames::zero();
}; // <-- simple

const UnitTest pregap_first = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 00 00:00:00
            INDEX 01 00:02:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;
    const auto tracks = Cue::parse(cue_txt, unknown_tags);

    check(tracks.size()) == 3uz;
    test(unknown_tags.empty());

    for (auto& t : tracks) {
        check(t.meta.artist)  == "Monk";
        check(t.meta.year)    == 2073;
        check(t.meta.comment) == "Test data";
        check(t.meta.disc_id) == "12345678";
        check(t.meta.album)   == "Among Us";
        check(t.file)         == "Among Us.flac";
    }

    check(tracks[0].meta.title) == "Sus";
    check(tracks[0].start)      == stdc::seconds{2};
    check(tracks[0].end)        == stdc::minutes{3} + stdc::seconds{39};

    check(tracks[1].meta.title) == "Impostor";
    check(tracks[1].start)      == tracks[0].end;
    check(tracks[1].end)        == stdc::minutes{5}
                                 + stdc::seconds{50}
                                 + Cue::Frames{15};

    check(tracks[2].meta.title) == "Electrical (Instrumental)";
    check(tracks[2].start)      == tracks[1].end;
    check(tracks[2].end)        == Cue::Frames::zero();
}; // <-- pregap_first

const UnitTest pregap_second = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 01 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 00 03:30:00
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;
    const auto tracks = Cue::parse(cue_txt, unknown_tags);

    check(tracks.size()) == 3uz;
    test(unknown_tags.empty());

    for (auto& t : tracks) {
        check(t.meta.artist)  == "Monk";
        check(t.meta.year)    == 2073;
        check(t.meta.comment) == "Test data";
        check(t.meta.disc_id) == "12345678";
        check(t.meta.album)   == "Among Us";
        check(t.file)         == "Among Us.flac";
    }

    check(tracks[0].meta.title) == "Sus";
    check(tracks[0].start)      == Cue::Frames::zero();
    check(tracks[0].end)        == stdc::minutes{3} + stdc::seconds{30};

    check(tracks[1].meta.title) == "Impostor";
    check(tracks[1].start)      == stdc::minutes{3} + stdc::seconds{39};
    check(tracks[1].end)        == stdc::minutes{5}
                                 + stdc::seconds{50}
                                 + Cue::Frames{15};

    check(tracks[2].meta.title) == "Electrical (Instrumental)";
    check(tracks[2].start)      == tracks[1].end;
    check(tracks[2].end)        == Cue::Frames::zero();
}; // <-- pregap_second

const UnitTest no_index01 = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 00 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 00 03:30:00
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;

    try {
        const auto _ = Cue::parse(cue_txt, unknown_tags);
    } catch (const std::exception& e) {
        const std::string_view msg{ e.what() };
        check(msg.find("No INDEX 01 in track")) != std::string_view::npos;
        return;
    }
    test(false);
}; // <-- no_index01

const UnitTest index_descending = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 00 00:02:00
            INDEX 01 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 00 03:30:00
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;

    try {
        const auto _ = Cue::parse(cue_txt, unknown_tags);
    } catch (const std::exception& e) {
        const std::string_view msg{ e.what() };
        check(msg.find("Out-of-order timestamp")) != std::string_view::npos;
        return;
    }
    test(false);
}; // <-- index_descending

const UnitTest index_num_descending = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us.flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 01 00:02:00
            INDEX 00 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 00 03:30:00
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 05:50:15
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;

    try {
        const auto _ = Cue::parse(cue_txt, unknown_tags);
    } catch (const std::exception& e) {
        const std::string_view msg{ e.what() };
        check(msg.find("Out-of-order index")) != std::string_view::npos;
        return;
    }
    test(false);
}; // <-- index_num_descending

} // <-- namespace single_file

namespace multi_file {

const UnitTest simple = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
        FILE "Among Us (Side A).flac" WAVE
          TRACK 01 AUDIO
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 01 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 03:39:00
        FILE "Among Us (Side B).flac" WAVE
          TRACK 03 AUDIO
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 00:02:00
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;
    const auto tracks = Cue::parse(cue_txt, unknown_tags);

    check(tracks.size()) == 3uz;
    test(unknown_tags.empty());

    for (auto& t : tracks) {
        check(t.meta.artist)  == "Monk";
        check(t.meta.year)    == 2073;
        check(t.meta.comment) == "Test data";
        check(t.meta.disc_id) == "12345678";
        check(t.meta.album)   == "Among Us";
    }

    check(tracks[0].meta.title) == "Sus";
    check(tracks[0].start)      == Cue::Frames::zero();
    check(tracks[0].end)        == stdc::minutes{3} + stdc::seconds{39};
    check(tracks[0].file)       == "Among Us (Side A).flac";

    check(tracks[1].meta.title) == "Impostor";
    check(tracks[1].start)      == tracks[0].end;
    check(tracks[1].end)        == Cue::Frames::zero(); // Until the end of
                                                        // the file
    check(tracks[1].file)       == "Among Us (Side A).flac";

    check(tracks[2].meta.title) == "Electrical (Instrumental)";
    check(tracks[2].start)      == stdc::seconds{2};
    check(tracks[2].end)        == Cue::Frames::zero();
    check(tracks[2].file)       == "Among Us (Side B).flac";
}; // <-- simple

const UnitTest file_in_track = [] {
    static constexpr auto cue_txt = R"cue(
        REM GENRE "Hard Rock"
        REM DATE 2073
        REM DISCID 12345678
        REM COMMENT "Test data"
        PERFORMER "Monk"
        TITLE "Among Us"
          TRACK 01 AUDIO
            FILE "Among Us (Side A).flac" WAVE
            TITLE "Sus"
            PERFORMER "Monk"
            INDEX 01 00:00:00
          TRACK 02 AUDIO
            TITLE "Impostor"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 03:39:00
          TRACK 03 AUDIO
            FILE "Among Us (Side B).flac" WAVE
            TITLE "Electrical (Instrumental)"
            PERFORMER "Monk"
            REM COMPOSER ""
            INDEX 01 00:02:00
    )cue"sv;
    std::vector<std::pair<std::string, std::string>> unknown_tags;
    const auto tracks = Cue::parse(cue_txt, unknown_tags);

    check(tracks.size()) == 3uz;
    test(unknown_tags.empty());

    for (auto& t : tracks) {
        check(t.meta.artist)  == "Monk";
        check(t.meta.year)    == 2073;
        check(t.meta.comment) == "Test data";
        check(t.meta.disc_id) == "12345678";
        check(t.meta.album)   == "Among Us";
    }

    check(tracks[0].meta.title) == "Sus";
    check(tracks[0].start)      == Cue::Frames::zero();
    check(tracks[0].end)        == stdc::minutes{3} + stdc::seconds{39};
    check(tracks[0].file)       == "Among Us (Side A).flac";

    check(tracks[1].meta.title) == "Impostor";
    check(tracks[1].start)      == tracks[0].end;
    check(tracks[1].end)        == Cue::Frames::zero(); // Until the end of
                                                        // the file
    check(tracks[1].file)       == "Among Us (Side A).flac";

    check(tracks[2].meta.title) == "Electrical (Instrumental)";
    check(tracks[2].start)      == stdc::seconds{2};
    check(tracks[2].end)        == Cue::Frames::zero();
    check(tracks[2].file)       == "Among Us (Side B).flac";
}; // <-- file_in_track

} // <-- namespace multi_file

} // <-- namespace test::qsefs::cue::load

} // <-- namespace <anonymous>
