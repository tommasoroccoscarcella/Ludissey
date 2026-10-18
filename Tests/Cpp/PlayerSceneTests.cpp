#include "Engine/Rendering/PlayerScene.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Golden.hpp"
#include "Interface/Canvas.hpp"

#include <cstdlib>
#include <filesystem>

using namespace ld;

namespace {
struct PlayerFixture {
    Experience world = Experience::blank();
    std::shared_ptr<PlayerGameplay> gameplay = std::make_shared<PlayerGameplay>();
    std::unique_ptr<render::PlayerScene> scene;
    double time = 0;
    PlayerFixture() {
        golden::registry();
        scene = std::make_unique<render::PlayerScene>(world, AvatarConfiguration::standard("Giocatrice", "female"), gameplay);
    }
    void step(int count = 1, double delta = 1. / 60) {
        for (int i = 0; i < count; ++i) { time += delta; scene->tick(time, delta, 1000 + time); }
    }
};
}

TEST_CASE("Player: locomozione, morte e respawn", "[player][scene]") {
    PlayerFixture f;
    CHECK(f.gameplay->loaded);
    auto start = f.scene->state();
    f.scene->key(13, true);
    f.step(60);
    CHECK(f.scene->state().z < start.z - 3);
    f.scene->key(53, true);
    CHECK(f.gameplay->menuOpen);
    auto paused = f.scene->state();
    f.step(30);
    CHECK(f.scene->state().z == Catch::Approx(paused.z));
    f.scene->key(53, true);
    f.step(30);
    CHECK(f.scene->state().z == Catch::Approx(paused.z));
    f.scene->key(49, true);
    f.step(5);
    CHECK(f.scene->state().y > paused.y);
    f.scene->key(49, false);
    f.gameplay->reset();
    f.step();
    CHECK(f.gameplay->dead);
    CHECK(f.gameplay->deaths == 1);
    CHECK(f.gameplay->health == 0);
    f.step(static_cast<int>((rules::respawnSeconds + .1) * 60));
    CHECK_FALSE(f.gameplay->dead);
    CHECK(f.gameplay->health == PlayerGameplay::maximumHealth);
    CHECK(f.gameplay->local().has_value());
    f.scene.reset();
    CHECK_FALSE(f.gameplay->loaded);
    CHECK_FALSE(f.gameplay->activate);
    CHECK_FALSE(f.gameplay->local());
}

TEST_CASE("Player: parti create da Lim", "[player][scene][lim]") {
    PlayerFixture f;
    f.scene->runtime().engine->execute("let p = Part(parent: World)\np.name = \"CreataDaLim\"\np.anchored = true\np.position = Vec3(2, 4, 6)", ScriptContext::server, "Player.Prova");
    f.step();
    auto part = f.scene->runtime().model->workspace()->child("CreataDaLim");
    REQUIRE(part);
    auto rendered = f.scene->scene().root->child(part->id, true);
    REQUIRE(rendered);
    CHECK(rendered->position == Vec3{2, 4, 6});
    f.scene->runtime().model->destroy(part);
    f.step();
    CHECK_FALSE(f.scene->scene().root->child(part->id, true));
    for (const auto& line : f.gameplay->console) CHECK_FALSE(line.error());
}

TEST_CASE("Player: avatar remoti e camera", "[player][scene]") {
    PlayerFixture f;
    RemoteAvatar remote;
    remote.session = SessionId("remote-session");
    remote.name = "Amico";
    remote.receive(AvatarSnapshot{1, 1, 1, 0, false, 1000}, 1000);
    remote.receive(AvatarSnapshot{2, 1, 1, 0, true, 1000.1}, 1000.1);
    f.gameplay->setRemotes({{remote.session, remote}});
    f.step(20);
    REQUIRE(f.scene->scene().root->child("remote.remote-session"));
    auto before = f.scene->camera().position;
    f.scene->look(20, 10);
    f.step();
    CHECK(f.scene->camera().position != before);
    f.gameplay->setRemotes({});
    f.step();
    CHECK_FALSE(f.scene->scene().root->child("remote.remote-session"));
    CHECK_FALSE(f.gameplay->chatOpen);
    CHECK_FALSE(f.gameplay->chatFocused);
    f.scene->key(44, true);
    CHECK(f.gameplay->chatOpen);
    CHECK(f.gameplay->chatFocused);
    auto eye = f.scene->camera().position;
    f.scene->look(20, 10);
    f.step();
    CHECK(f.scene->camera().position == eye);
}

TEST_CASE("Player: menu Esc e tasti rapidi", "[player][scene][menu]") {
    PlayerFixture f;
    f.step(5);
    f.scene->key(53, true);
    f.scene->key(53, false);
    CHECK(f.gameplay->menuOpen);
    CHECK(f.gameplay->menuTab == "Giocatori");
    f.scene->key(37, true);
    f.scene->key(37, false);
    CHECK(f.gameplay->leaveRequested);
    f.gameplay->leaveRequested = false;
    f.scene->key(15, true);
    f.scene->key(15, false);
    f.step(2);
    CHECK(f.gameplay->dead);
    CHECK(f.gameplay->deaths == 1);
    f.step(static_cast<int>((rules::respawnSeconds + 0.5) * 60));
    CHECK_FALSE(f.gameplay->dead);
    CHECK(f.gameplay->health == PlayerGameplay::maximumHealth);
    f.gameplay->menuOpen = false;
    Experience empty = Experience::blank();
    empty.blocks.clear();
    PlayerFixture falling;
    falling.scene = std::make_unique<render::PlayerScene>(empty, AvatarConfiguration::standard("Giocatrice", "female"), falling.gameplay);
    falling.step(5 * 60);
    CHECK(falling.gameplay->deaths >= 1);
}

TEST_CASE("Player: etichette dei nomi", "[player][scene][nametag]") {
    PlayerFixture f;
    auto tags = [&] {
        std::vector<const render::Node*> found;
        std::function<void(const render::Node&)> walk = [&](const render::Node& n) {
            if (n.name == render::name_tag::name) found.push_back(&n);
            for (const auto& c : n.children()) walk(*c);
        };
        walk(*f.scene->scene().root);
        return found;
    };
    f.step(5);
    REQUIRE(tags().size() == 1);
    const render::Node* local = tags().front()->parent();
    RemoteAvatar remote;
    remote.session = SessionId("remote-session");
    remote.name = "Compagno";
    remote.receive(AvatarSnapshot{3, 1, 3, 0, false, 1000}, 1000);
    remote.receive(AvatarSnapshot{3, 1, 3, 0, false, 1000.1}, 1000.1);
    f.gameplay->setRemotes({{remote.session, remote}});
    f.step(10);
    auto both = tags();
    REQUIRE(both.size() == 2);
    auto owner = f.scene->scene().root->child("remote.remote-session");
    REQUIRE(owner);
    CHECK(std::count_if(both.begin(), both.end(), [&](const render::Node* t) { return t->parent() == owner.get(); }) == 1);
    CHECK(std::count_if(both.begin(), both.end(), [&](const render::Node* t) { return t->parent() == local; }) == 1);
    f.scene->key(53, true);
    f.scene->key(15, true);
    f.scene->key(53, true);
    f.step(static_cast<int>((rules::respawnSeconds + 0.5) * 60));
    CHECK_FALSE(f.gameplay->dead);
    both = tags();
    CHECK(both.size() == 2);
    CHECK(std::any_of(both.begin(), both.end(), [&](const render::Node* t) { return t->parent() == local; }));
    for (const auto* t : both) {
        CHECK(t->scale.x >= render::name_tag::low - 1e-9);
        CHECK(t->scale.x <= render::name_tag::high + 1e-9);
    }
    auto holder = render::Node::make("holder");
    holder->add(render::name_tag::make("Prova"));
    auto probe = holder->child(render::name_tag::name);
    render::name_tag::fit({holder}, {0, render::name_tag::lift, 1});
    CHECK(std::abs(probe->scale.x - render::name_tag::low) < 1e-6);
    render::name_tag::fit({holder}, {0, render::name_tag::lift, 10});
    CHECK(std::abs(probe->scale.x - 10 * render::name_tag::perUnit) < 1e-6);
    render::name_tag::fit({holder}, {0, render::name_tag::lift, 200});
    CHECK(std::abs(probe->scale.x - render::name_tag::high) < 1e-6);
    f.gameplay->setRemotes({});
    f.step(2);
    auto remaining = tags();
    REQUIRE(remaining.size() == 1);
    CHECK(remaining.front()->parent() == local);
}

TEST_CASE("Player: prima persona sopra il Baseplate", "[player][scene][camera][rendering]") {
    auto& renderer = render::Renderer::shared();
    REQUIRE(renderer.available());
    PlayerFixture f;
    f.step(40);
    for (int i = 0; i < 30; ++i) f.scene->zoom(4, false);
    f.step(10);
    auto forward = [&] {
        Mat4 w = f.scene->camera().world();
        return Vec3{-w.at(0, 2), -w.at(1, 2), -w.at(2, 2)}.normalized();
    };
    double dy = 40;
    for (int i = 0; i < 40 && forward().y > -0.9; ++i) {
        double before = forward().y;
        f.scene->look(0, dy);
        f.step();
        if (forward().y > before) dy = -dy;
    }
    INFO("direzione della camera " << forward().x << ", " << forward().y << ", " << forward().z);
    REQUIRE(forward().y < -0.85);
    f.step(5);
    auto image = renderer.snapshot(f.scene->scene(), f.scene->camera(), 96, 96, 1, false);
    REQUIRE(image.width == 96);
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
        std::filesystem::create_directories(folder);
        ui::Canvas canvas(image.width, image.height, 1);
        canvas.image(ui::imageFromRgba(image.rgba, image.width, image.height), {0, 0, static_cast<double>(image.width), static_cast<double>(image.height)});
        canvas.writePng(std::filesystem::path(folder) / "prima persona verso il basso.png");
    }
    Color sky = f.scene->scene().background.color;
    int hits = 0;
    for (int y = 40; y < 56; ++y)
        for (int x = 40; x < 56; ++x) {
            const auto* p = &image.rgba[static_cast<std::size_t>((y * 96 + x) * 4)];
            double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
            bool background = std::abs(r - sky.r) + std::abs(g - sky.g) + std::abs(b - sky.b) < 0.15;
            bool neutral = std::abs(r - g) < 0.08 && std::abs(g - b) < 0.12;
            if (!background && neutral) ++hits;
        }
    CHECK(hits > 16 * 16 * 3 / 4);
}
