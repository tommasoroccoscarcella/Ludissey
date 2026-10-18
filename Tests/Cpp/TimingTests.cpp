#include "Tests/Support/Timing.hpp"

#include <Catch2/catch_amalgamated.hpp>

TEST_CASE("Misure: condizioni non valide", "[timing]") {
    using namespace support::timing;
    CHECK(classify(false, false, true, 0, 0, 4) == Validity::powerLimited);
    CHECK(classify(false, false, false, 0, 0, 4) == Validity::valid);
    CHECK(classify(false, false, false, 2, 0, 4) == Validity::throttled);
    CHECK(classify(false, false, false, 0, 4, 4) == Validity::loaded);
    CHECK(classify(true, false, true, 2, 8, 4) == Validity::valid);
    CHECK(classify(false, true, false, 0, 0, 4) == Validity::instrumented);
    CHECK(classify(true, true, true, 2, 8, 4) == Validity::instrumented);
    CHECK(classify(false, false, false, -1, 0, 4) == Validity::unavailable);
    CHECK(classify(false, false, false, 0, -1, 4) == Validity::unavailable);
    CHECK(classify(false, false, false, 0, 0, 0) == Validity::unavailable);
    CHECK(classify(true, false, false, -1, -1, 0) == Validity::valid);
}
