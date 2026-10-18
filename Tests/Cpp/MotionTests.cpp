#include "Engine/Character/Character.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Rendering/Scene.hpp"
#include "Golden.hpp"

#include <cmath>
#include <random>

using namespace ld;

namespace {

WorldBlock solid(double x, double y, double z, double sx, double sy, double sz, double rotation = 0) {
    WorldBlock b;
    b.x = x; b.y = y; b.z = z;
    b.sizeX = sx; b.sizeY = sy; b.sizeZ = sz;
    b.rotationY = rotation;
    return b;
}

bool inside(Vec3 p, const WorldBlock& b) {
    Vec3 half = b.halfExtents();
    return std::abs(p.x - b.x) < half.x && std::abs(p.y - b.y) < half.y && std::abs(p.z - b.z) < half.z;
}

CharacterInput input(double forward, double strafe) {
    CharacterInput i;
    i.forward = forward;
    i.strafe = strafe;
    return i;
}

const Terrain none;

}

TEST_CASE("Movimento: modello dritto e orientato", "[motion][rotation]") {
    auto node = render::Node::make();
    for (double camera : {0.0, pi / 2, pi, -pi / 2}) {
        for (auto pressed : {input(1, 0), input(-1, 0), input(0, 1), input(0, -1), input(1, 1)}) {
            CharacterState state;
            for (int frame = 0; frame < 90; ++frame) {
                state = motion::step(state, pressed, camera, {}, none, 1.0 / 60);
                node->setYaw(state.yaw);
                Vec3 up = node->orientation.rotate({0, 1, 0});
                Vec3 front = node->orientation.rotate({0, 0, 1});
                REQUIRE(up.y == Catch::Approx(1).margin(1e-9));
                REQUIRE(front.x == Catch::Approx(std::sin(state.yaw)).margin(1e-9));
                REQUIRE(front.z == Catch::Approx(std::cos(state.yaw)).margin(1e-9));
            }
            Vec2 move = motion::direction(pressed, camera);
            Vec3 front = node->orientation.rotate({0, 0, 1});
            CHECK(front.x * move.x + front.z * move.y > 0.999);
        }
    }
}

TEST_CASE("Movimento: migrazione del WorldBlock", "[motion]") {
    auto legacy = Json::parse(R"({"id":"00000000-0000-4000-8000-000000000000","kind":"cube","color":"6633EE","x":1.5,"y":2,"z":-3,"size":4})");
    REQUIRE(legacy);
    auto migrated = WorldBlock::fromJson(*legacy);
    REQUIRE(migrated);
    CHECK(migrated->sizeX == 4);
    CHECK(migrated->sizeY == 4);
    CHECK(migrated->sizeZ == 4);
    CHECK(migrated->rotationX == 0);
    CHECK(migrated->rotationY == 0);
    CHECK(migrated->rotationZ == 0);
    CHECK(migrated->canCollide);
    CHECK(migrated->anchored);
    CHECK_FALSE(migrated->locked);
    CHECK(migrated->material == "plastic");
    CHECK(migrated->transparency == 0);
    CHECK(migrated->reflectance == 0);
    CHECK(migrated->parent == "Workspace");
    WorldBlock rotated = *migrated;
    rotated.rotationY = 45; rotated.sizeX = 2; rotated.sizeY = 6; rotated.sizeZ = 3;
    auto round = WorldBlock::fromJson(rotated.json());
    REQUIRE(round);
    CHECK(*round == rotated);
    CHECK(rotated.json().contains("size"));
}

TEST_CASE("Movimento: 32 casi relativi alla camera", "[motion]") {
    std::vector<std::pair<std::string, double>> cameras = {{"0", 0}, {"90", pi / 2}, {"180", pi}, {"270", 3 * pi / 2}};
    std::vector<std::pair<std::string, CharacterInput>> inputs = {{"W", input(1, 0)}, {"A", input(0, -1)}, {"S", input(-1, 0)}, {"D", input(0, 1)},
                                                                  {"W+A", input(1, -1)}, {"W+D", input(1, 1)}, {"S+A", input(-1, -1)}, {"S+D", input(-1, 1)}};
    for (const auto& [label, cameraYaw] : cameras)
        for (const auto& [name, pressed] : inputs) {
            INFO("camera " << label << "° " << name);
            Vec2 move = motion::direction(pressed, cameraYaw);
            CharacterState state;
            double previous = state.yaw, jump = 0, alignment20 = 0;
            for (int frame = 0; frame < 40; ++frame) {
                state = motion::step(state, pressed, cameraYaw, {}, none, 1.0 / 60);
                if (frame >= 20) jump = std::max(jump, std::abs(motion::shortestAngle(previous, state.yaw)));
                if (frame == 19) alignment20 = std::abs(motion::shortestAngle(state.yaw, motion::heading(move.x, move.y)));
                previous = state.yaw;
            }
            CHECK(alignment20 < 0.05);
            double travelled = std::sqrt(state.x * state.x + state.z * state.z);
            REQUIRE(travelled > 0.1);
            CHECK((state.x / travelled) * move.x + (state.z / travelled) * move.y > 0.999);
            CHECK(std::abs(motion::shortestAngle(state.yaw, motion::heading(move.x, move.y))) < 0.02);
            CHECK(jump < 0.02);
        }
    CharacterState still;
    still.x = 3; still.z = -2; still.yaw = 1.2;
    double before = still.yaw;
    for (double angle : {0.0, pi / 2, pi, 3 * pi / 2}) still = motion::step(still, CharacterInput{}, angle, {}, none, 1.0 / 60);
    CHECK(still.yaw == before);
    CHECK(std::abs(still.x - 3) < 0.001);
    CHECK(std::abs(still.z + 2) < 0.001);
    double positive = 179 * pi / 180, negative = -179 * pi / 180;
    CHECK(std::abs(degrees(motion::shortestAngle(positive, negative))) < 3);
    CHECK(std::abs(degrees(motion::shortestAngle(negative, positive))) < 3);
    CharacterState wrapping;
    wrapping.yaw = positive;
    double maximum = 0, reference = wrapping.yaw;
    for (int i = 0; i < 40; ++i) {
        wrapping = motion::step(wrapping, input(1, 0), pi / 180, {}, none, 1.0 / 60);
        maximum = std::max(maximum, std::abs(motion::shortestAngle(reference, wrapping.yaw)));
        reference = wrapping.yaw;
    }
    CHECK(maximum < 1.0);
}

TEST_CASE("Movimento: mondo reale, spawn e nuoto", "[motion]") {
    CharacterState floorState;
    floorState.y = 6;
    WorldBlock platform = solid(0, 1, 0, 6, 2, 6);
    for (int i = 0; i < 240; ++i) floorState = motion::step(floorState, CharacterInput{}, 0, {platform}, none, 1.0 / 60);
    CHECK(std::abs(floorState.y - 2) < 0.01);
    WorldBlock ghost = platform;
    ghost.canCollide = false;
    WorldBlock ground = Experience::baseplate("11111111-2222-4333-8444-555555555555");
    CharacterState ghostState;
    ghostState.y = 6;
    for (int i = 0; i < 240; ++i) ghostState = motion::step(ghostState, CharacterInput{}, 0, {ghost, ground}, none, 1.0 / 60);
    CHECK(std::abs(ghostState.y) < 0.01);
    CharacterState voidState;
    voidState.y = 6;
    for (int frames = 0; !motion::fallen(voidState) && frames < 600; ++frames) voidState = motion::step(voidState, CharacterInput{}, 0, {}, none, 1.0 / 60);
    CHECK(motion::fallen(voidState));
    CharacterState edge;
    edge.x = 29;
    for (int i = 0; i < 60; ++i) edge = motion::step(edge, input(0, 1), 0, {ground}, none, 1.0 / 60);
    CHECK(edge.x > 30.5);
    CHECK(edge.y < 0);
    auto landing = motion::spawn({ground, platform}, none);
    CHECK(std::abs(landing.y) < 0.001);
    CHECK(landing.z == 5);
    Terrain pool;
    TerrainBrush brush;
    brush.radius = 3;
    brush.strength = 1;
    brush.material = TerrainMaterial::grass;
    for (double x = -9; x <= 9; x += 3)
        for (double z = -9; z <= 9; z += 3) pool.apply(TerrainTool::water, {x, 2, z}, brush);
    CharacterState diver;
    diver.y = 3;
    for (int i = 0; i < 60; ++i) diver = motion::step(diver, CharacterInput{}, 0, {ground}, pool, 1.0 / 60);
    CHECK(diver.swimming);
    CHECK(diver.y > 0.5);
    double sinking = diver.y, highest = sinking;
    CharacterInput rise;
    rise.ascend = true;
    for (int i = 0; i < 90; ++i) {
        diver = motion::step(diver, rise, 0, {ground}, pool, 1.0 / 60);
        highest = std::max(highest, diver.y);
    }
    CHECK(highest > sinking + 0.3);
    CharacterState walker, swimmer;
    walker.x = -5;
    swimmer.x = -5;
    swimmer.y = 1;
    for (int i = 0; i < 60; ++i) {
        walker = motion::step(walker, input(0, 1), 0, {ground}, none, 1.0 / 60);
        swimmer = motion::step(swimmer, input(0, 1), 0, {ground}, pool, 1.0 / 60);
    }
    CHECK(swimmer.x + 5 < (walker.x + 5) * 0.7);
    CHECK(camera_rig::clearance({0, 1, 0}, 0, 0, 12, {}, pool) == 12);
    auto raised = motion::spawn({ground, platform}, none, 0, 0);
    CHECK(std::abs(raised.y - 2) < 0.001);
}

TEST_CASE("Camera: zoom e collisioni", "[motion][camera]") {
    WorldBlock baseplate = solid(0, -0.5, 0, 60, 1, 60);
    Vec3 head{0, 0.9, 0};
    auto below = camera_rig::place(head, 0, -1.2, 12, {baseplate}, none);
    CHECK(below.position.y >= baseplate.y + 0.5);
    CHECK((below.firstPerson || below.position.y > head.y - 2));
    auto free = camera_rig::place(head, 0, 0.3, 12, {baseplate}, none);
    CHECK(std::abs(free.position.y - (head.y + std::sin(0.3) * 12)) < 0.01);
    WorldBlock wall = solid(0, 2, 5, 20, 8, 1);
    CHECK(camera_rig::clearance(head, 0, 0, 12, {wall}, none) < 5);
    WorldBlock ghostWall = wall;
    ghostWall.canCollide = false;
    CHECK(camera_rig::clearance(head, 0, 0, 12, {ghostWall}, none) == 12);
    double zoomed = 12;
    for (int i = 0; i < 40; ++i) zoomed = camera_rig::zoom(zoomed, 0.2);
    CHECK(camera_rig::firstPerson(zoomed));
    double pulled = zoomed;
    for (int i = 0; i < 40; ++i) pulled = camera_rig::zoom(pulled, -0.2);
    CHECK_FALSE(camera_rig::firstPerson(pulled));
    CHECK(pulled <= camera_rig::maximumDistance);

    for (double yaw : {0.0, pi / 4, pi / 2, 3 * pi / 4, pi, -pi / 4, -pi / 2, -3 * pi / 4}) {
        INFO("yaw " << yaw);
        Vec3 forward = world_space::forward(yaw, 0);
        Vec2 moved = motion::direction(input(1, 0), yaw);
        CHECK(forward.x * moved.x + forward.z * moved.y > 0.999);
        Vec2 back = motion::direction(input(-1, 0), yaw);
        CHECK(forward.x * back.x + forward.z * back.y < -0.999);
        Vec2 strafe = motion::direction(input(0, 1), yaw);
        CHECK(std::abs(forward.x * strafe.x + forward.z * strafe.y) < 0.001);
        Vec3 right = world_space::right(yaw);
        CHECK(right.x * strafe.x + right.z * strafe.y > 0.999);
        Vec2 left = motion::direction(input(0, -1), yaw);
        CHECK(right.x * left.x + right.z * left.y < -0.999);
        Vec2 mixed = motion::direction(input(1, 1), yaw);
        CHECK(std::abs(std::sqrt(mixed.x * mixed.x + mixed.y * mixed.y) - 1) < 0.001);
        for (double pitch : {-1.2, -0.4, 0.0, 0.4, 1.2}) {
            Vec3 aimed = world_space::forward(yaw, pitch);
            double flat = std::sqrt(aimed.x * aimed.x + aimed.z * aimed.z);
            CHECK((flat < 0.0001 || aimed.x / flat * moved.x + aimed.z / flat * moved.y > 0.999));
        }
        Vec3 orbit = world_space::orbit(yaw, 0, 8);
        CHECK(orbit.x * moved.x + orbit.z * moved.y < -0.999 * 8);
    }

    Vec3 eye{0, 1, 0};
    WorldBlock thin = solid(0, 1, 4, 20, 8, 0.2);
    CHECK(camera_rig::clearance(eye, 0, 0, 20, {thin}, none) < 4);
    WorldBlock thick = solid(0, 1, 4, 20, 8, 4);
    CHECK(camera_rig::clearance(eye, 0, 0, 20, {thick}, none) < 2.5);
    WorldBlock turned = solid(0, 1, 5, 20, 8, 0.2, 0.6);
    CHECK(camera_rig::clearance(eye, 0, 0, 20, {turned}, none) < 5);
    std::vector<WorldBlock> corner = {solid(3, 1, 4, 6, 8, 0.4), solid(4, 1, 3, 0.4, 8, 6)};
    CHECK(camera_rig::clearance(eye, 0.7, 0, 20, corner, none) < 6);
    WorldBlock floor = solid(0, -0.5, 0, 80, 1, 80);
    CHECK(camera_rig::clearance(eye, 0, -1.4, 20, {floor}, none) < 2.5);
    WorldBlock ceiling = solid(0, 4, 0, 80, 1, 80);
    CHECK(camera_rig::clearance(eye, 0, 1.4, 20, {ceiling}, none) < 3.5);
    int worst = 0;
    for (int step = 0; step < 720; ++step) {
        auto placed = camera_rig::place(eye, step * pi / 90, 0.2, 20, {thin, thick, floor, ceiling}, none);
        for (const auto& b : {thin, thick, floor, ceiling})
            if (inside(placed.position, b)) ++worst;
    }
    CHECK(worst == 0);
    double zoomLevel = 20;
    for (int i = 0; i < 120; ++i) {
        zoomLevel = camera_rig::zoom(zoomLevel, 0.15);
        CHECK_FALSE(inside(camera_rig::place(eye, 0, 0, zoomLevel, {thin}, none).position, thin));
    }
    auto near = camera_rig::place({0, 1, 3.6}, 0, 0, 12, {thin}, none);
    CHECK((near.firstPerson || near.position.z < thin.z - thin.halfExtents().z));
    auto transition = camera_rig::place(eye, 0, 0, camera_rig::firstPersonDistance, {thin}, none);
    CHECK(transition.firstPerson);
    CHECK(transition.position.x == eye.x);
    CHECK(transition.position.z == eye.z);
    double recovered = camera_rig::clearance(eye, 0, 0, 20, {thin}, none);
    for (int i = 0; i < 120; ++i) recovered = camera_rig::recover(recovered, 20, 1.0 / 60);
    CHECK(std::abs(recovered - 20) < 0.05);
    WorldBlock passable = thin;
    passable.canCollide = false;
    CHECK(camera_rig::clearance(eye, 0, 0, 20, {passable}, none) == 20);
}

TEST_CASE("Personaggio: passo fisso e frequenze", "[motion][driver]") {
    WorldBlock ground = solid(0, -0.5, 0, 200, 1, 200);
    WorldBlock stepBlock = solid(0, 0.15, -6, 8, 0.3, 4);
    struct Run {
        CharacterState end;
        std::vector<double> perVsync, rendered;
    };
    std::mt19937 random(2026);
    auto run = [&](double hertz, double jitter, double seconds) {
        CharacterDriver driver;
        driver.reset(CharacterState{});
        FramePacer pacer;
        double refresh = 1 / hertz, last = 0;
        Run out;
        std::optional<CharacterPose> previous;
        std::uniform_real_distribution<double> noise(-jitter, jitter);
        for (int frame = 1; frame <= static_cast<int>(seconds * hertz); ++frame) {
            double clock = frame * refresh + (jitter > 0 ? noise(random) : 0);
            double delta = pacer.next(clock - last, refresh);
            last = clock;
            driver.advance(delta, [&](const CharacterState& s, double step) -> std::optional<CharacterState> { return motion::step(s, input(1, 0), 0, {ground, stepBlock}, none, step); });
            auto pose = driver.pose(delta);
            if (previous) out.perVsync.push_back(std::abs(pose.z - previous->z) / (delta / refresh));
            out.rendered.push_back(pose.y);
            previous = pose;
        }
        out.end = driver.state();
        return out;
    };
    std::vector<Run> frames = {run(30, 0.004, 1), run(60, 0.004, 1), run(120, 0.004, 1)};
    double spread = 0;
    for (const auto& f : frames) spread = std::max(spread, std::abs(f.end.z - frames[1].end.z));
    CHECK(spread < 0.07);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        INFO((i == 0 ? 30 : i == 1 ? 60 : 120) << " Hz");
        std::vector<double> steady(frames[i].perVsync.begin() + 2, frames[i].perVsync.end());
        double mean = 0;
        for (double v : steady) {
            CHECK(v > 0);
            mean += v;
        }
        mean /= std::max<std::size_t>(1, steady.size());
        double worst = 0;
        for (double v : steady) worst = std::max(worst, std::abs(v - mean) / mean);
        CHECK(worst < 0.01);
    }
    auto climb = run(60, 0, 2);
    double jumps = 0;
    for (std::size_t i = 1; i < climb.rendered.size(); ++i) jumps = std::max(jumps, climb.rendered[i] - climb.rendered[i - 1]);
    CHECK(std::abs(climb.rendered.back() - 0.3) < 0.01);
    CHECK(jumps < 0.2);
    CharacterDriver driver;
    driver.reset(CharacterState{});
    driver.advance(1.0 / 60, [](const CharacterState& s, double) -> std::optional<CharacterState> {
        CharacterState next = s;
        next.x = 40;
        return next;
    });
    CHECK(driver.pose(1.0 / 60).x == 40);
}

TEST_CASE("Personaggio: posa dopo un gradino", "[player][motion]") {
    struct Moment {
        double height;
        bool grounded;
        double vertical;
    };
    auto play = [](const std::vector<Moment>& script, double frame) {
        CharacterDriver driver;
        CharacterState start;
        start.grounded = true;
        driver.reset(start);
        driver.pose(frame);
        std::size_t index = 0;
        std::vector<double> shown, aimed;
        while (index < script.size()) {
            driver.advance(frame, [&](const CharacterState& s, double) -> std::optional<CharacterState> {
                CharacterState next = s;
                const Moment& moment = script[std::min(index, script.size() - 1)];
                next.y = moment.height;
                next.grounded = moment.grounded;
                next.vertical = moment.vertical;
                ++index;
                return next;
            });
            shown.push_back(driver.pose(frame).y);
            aimed.push_back(driver.previous().y + (driver.state().y - driver.previous().y) * driver.alpha());
        }
        return std::pair{shown, aimed};
    };
    std::vector<Moment> ledge{{0, true, 0}, {0, true, 0}, {0.6, true, 0}};
    for (int i = 1; i <= 60; ++i) ledge.push_back({0.6 - 0.005 * i, false, -0.3});
    for (double frame : {1.0 / 60, 1.0 / 120, 1.0 / 144, 1.0 / 30}) {
        auto [shown, aimed] = play(ledge, frame);
        double rise = 0;
        for (std::size_t i = 1; i < shown.size(); ++i) rise = std::max(rise, shown[i] - shown[i - 1]);
        INFO("frame " << frame << ", salita massima " << rise);
        CHECK(rise < motion::stepUp * 0.5);
        CHECK(std::abs(shown.back() - aimed.back()) < 0.02);
    }
    std::vector<Moment> leap{{0, true, 0}, {0, true, 0}};
    for (int i = 1; i <= 12; ++i) leap.push_back({0.5 * i, false, 30});
    for (double frame : {1.0 / 60, 1.0 / 120, 1.0 / 144}) {
        auto [shown, aimed] = play(leap, frame);
        for (std::size_t i = 0; i < shown.size(); ++i) CHECK(std::abs(aimed[i] - shown[i]) < 1e-9);
    }
}
