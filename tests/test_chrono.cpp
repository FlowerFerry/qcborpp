#include <qcborpp/qcborpp.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace qcborpp;

// quick chrono roundtrip — exercises encode + decode APIs
TEST_CASE("chrono: roundtrip time_point and days", "[chrono]") {
    uint8_t buf[256];
    encoder enc(byte_span{buf, sizeof(buf)});
    {
        auto m = enc.map();
        auto tp = std::chrono::system_clock::from_time_t(1710000000);
        m["created"]   = tp;
        m["expires_d"] = std::chrono::hours(48);
        m["plain"]     = int64_t(42);
    }
    auto data = enc.finish();

    decoder dec(data);
    {
        auto m = dec.map();

        auto tp2 = m["created"].as_time_point();
        auto d   = m["expires_d"].as_days_duration();
        auto sec = m["created"].as_date_epoch();
        auto days = m["expires_d"].as_days_epoch();
        auto plain = int64_t(m["plain"]);

        CHECK(sec == 1710000000);
        CHECK(days == 2);
        CHECK(d.count() == 2);
        CHECK(plain == 42);

        auto sec_tp = std::chrono::duration_cast<std::chrono::seconds>(
            tp2.time_since_epoch()).count();
        CHECK(sec_tp == 1710000000);
    }

    auto ec = dec.finish();
    CHECK(!ec);
}
