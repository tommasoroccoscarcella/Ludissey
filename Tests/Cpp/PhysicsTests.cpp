#include "Engine/Character/Character.hpp"
#include "Engine/Physics/Physics.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace ld;

namespace {

WorldBlock box(std::string id, double x, double y, double z, double size, bool anchored) {
    WorldBlock block;
    block.id = std::move(id);
    block.x = x;
    block.y = y;
    block.z = z;
    block.setSize(size);
    block.anchored = anchored;
    return block;
}

}

TEST_CASE("Fisica: caduta, attrito e ancoraggio", "[physics]") {
    Terrain terrain;
    WorldBlock floor = box("pavimento", 0, -0.5, 0, 10, true);
    floor.sizeY = 1;
    WorldBlock crate = box("cassa", 0, 4, 0, 1, false);
    std::vector<const WorldBlock*> solids{&floor, &crate};
    CHECK(physics::restingHeight(crate, solids, terrain) == Catch::Approx(0.5));
    physics::Body body{crate, {2, 0, 0}};
    double previous = body.block.y;
    for (int i = 0; i < 240; ++i) {
        body = physics::integrate(body, solids, terrain, 1.0 / 120);
        CHECK(body.block.y <= previous + 1e-12);
        previous = body.block.y;
    }
    CHECK(body.block.y == Catch::Approx(0.5));
    CHECK(body.velocity.y == 0);
    CHECK(std::fabs(body.velocity.x) < 0.01);
    CHECK(body.block.x > 0);
    physics::Body fixed{floor, {0, -3, 0}};
    CHECK(physics::integrate(fixed, solids, terrain, 0.1).block == floor);
    WorldBlock ghost = floor;
    ghost.canCollide = false;
    std::vector<const WorldBlock*> passable{&ghost, &crate};
    CHECK(physics::restingHeight(crate, passable, terrain) < -1e9);
    CHECK(physics::fallen(box("vuoto", 0, motion::voidHeight - 1, 0, 1, false)));
    CHECK_FALSE(physics::fallen(crate));
}

TEST_CASE("Fisica: contatto con il terreno", "[physics]") {
    Terrain terrain;
    REQUIRE(terrain.fillSphere({0, -4, 0}, 4, TerrainMaterial::grass));
    WorldBlock stone = box("sasso", 0, 6, 0, 1, false);
    double rest = physics::restingHeight(stone, {&stone}, terrain);
    CHECK(rest > -0.5);
    CHECK(rest < 1.5);
    physics::Capsule character{{0, 0, 0}, motion::radius, motion::height};
    auto step = physics::touching(box("gradino", 0, 0.2, 0, 0.4, true), character);
    REQUIRE(step);
    CHECK(step->feet);
    auto wall = physics::touching(box("muro", 0.6, 1, 0, 1, true), character);
    REQUIRE(wall);
    CHECK_FALSE(wall->feet);
    CHECK_FALSE(physics::touching(box("lontano", 5, 1, 0, 1, true), character));
    CHECK_FALSE(physics::touching(box("sopra", 0, 4, 0, 1, true), character));
}
