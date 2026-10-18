#include "Engine/Core/Json.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/Core/Signal.hpp"
#include "Engine/Core/Text.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace ld;

TEST_CASE("JSON round trip keeps types and order", "[core][json]") {
    auto parsed = Json::parse(R"({"b":1,"a":[true,false,null,0.5,-3,"x\u00e8\n"],"c":{"d":1e21}})");
    REQUIRE(parsed.ok());
    const Json& value = *parsed;
    CHECK(value["b"].integer() == 1);
    CHECK(value["a"][0].boolean());
    CHECK(value["a"][2].isNull());
    CHECK(value["a"][3].number() == 0.5);
    CHECK(value["a"][5].string() == "x\xC3\xA8\n");
    CHECK(value.dump() == R"({"b":1,"a":[true,false,null,0.5,-3,"xè\n"],"c":{"d":1e+21}})");
    CHECK(value.dump(true).find("\"a\"") < value.dump(true).find("\"b\""));
    auto again = Json::parse(value.dump());
    REQUIRE(again.ok());
    CHECK(*again == value);
}

TEST_CASE("JSON rejects malformed input without crashing", "[core][json][fuzz]") {
    for (const char* bad : {"", "{", "[1,", "{\"a\" 1}", "01", "1.", "\"\\x\"", "\"\\ud800\"", "[1]]", "{\"a\":}", "nul", "-", "1e", "\"a\x01\""}) {
        CHECK_FALSE(Json::parse(bad).ok());
    }
    std::string deep(1000, '[');
    CHECK_FALSE(Json::parse(deep).ok());
}

TEST_CASE("Numbers format as shortest round trip", "[core][json]") {
    CHECK(formatNumber(0.1 + 0.2) == "0.30000000000000004");
    CHECK(formatNumber(1.0) == "1");
    CHECK(formatNumber(-0.25) == "-0.25");
    CHECK(formatNumber(1e-7) == "1e-7");
    CHECK(formatNumber(123456.5) == "123456.5");
}

TEST_CASE("Text helpers", "[core][text]") {
    auto id = makeUuid();
    CHECK(isUuid(id));
    CHECK(id[14] == '4');
    CHECK(lowercase(id) == id);
    CHECK(base64Encode("Ludissey") == "THVkaXNzZXk=");
    auto decoded = base64Decode("THVkaXNzZXk=");
    REQUIRE(decoded);
    CHECK(std::string(decoded->begin(), decoded->end()) == "Ludissey");
    CHECK_FALSE(base64Decode("abc"));
    CHECK_FALSE(base64Decode("a=bc"));
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(utf8Length("città") == 5);
    CHECK(utf8Prefix("città!", 4) == "citt");
    CHECK(utf8Prefix("città!", 5) == "città");
    CHECK(trim("  a b \n") == "a b");
    CHECK(split("1,2,,3", ',').size() == 4);
}

TEST_CASE("Math primitives", "[core][math]") {
    Mat4 m = Mat4::translation({1, 2, 3}) * Mat4::scale({2, 2, 2});
    Vec3 p = m.transformPoint({1, 1, 1});
    CHECK(p == Vec3{3, 4, 5});
    Vec3 back = m.inverted().transformPoint(p);
    CHECK(back.x == Catch::Approx(1));
    CHECK(Color::hex("9AA0A8").r == Catch::Approx(0x9A / 255.0));
    CHECK(Color::hex("zz", Color{1, 0, 0, 1}).r == 1);
    Quaternion q = Quaternion::between({0, 1, 0}, {1, 0, 0});
    Vec3 turned = q.rotate({0, 1, 0});
    CHECK(turned.x == Catch::Approx(1));
    CHECK(std::abs(turned.y) < 1e-9);
}

TEST_CASE("Signals disconnect safely during emit", "[core][signal]") {
    Signal<int> signal;
    int total = 0;
    Signal<int>::Connection second;
    auto first = signal.connect([&](int v) { total += v; second.disconnect(); });
    second = signal.connect([&](int v) { total += v * 100; });
    signal.emit(1);
    CHECK(total == 1);
    CHECK(signal.count() == 1);
    first.disconnect();
    signal.emit(1);
    CHECK(total == 1);
}
