#import <AppKit/AppKit.h>

#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Golden.hpp"
#include "Harbor.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Surface.hpp"
#include "Pixels.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Player/PlayerView.hpp"
#include "ServerHarness.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>

using namespace ld;
using namespace pixels;

namespace {

class GatedBackend final : public Backend {
public:
    std::shared_ptr<Backend> wrapped;
    std::string gate;
    std::string refusal;
    bool waiting = false;
    bool resume = false;
    Async<Json> cloud(std::string name, Json parameters) override {
        if (name == gate && !refusal.empty()) throw ServiceError(refusal);
        Json response = co_await wrapped->cloud(name, parameters);
        if (name == gate) {
            waiting = true;
            while (!resume) co_await sleepFor(0.005);
        }
        co_return response;
    }
    Async<Json> client(std::string name, Json parameters) override { co_return co_await wrapped->client(name, parameters); }
    std::shared_ptr<HttpTransport> storage() override { return wrapped->storage(); }
};

Pixels shrink(const Pixels& source, int width, int height) {
    Pixels out{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 255)};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int x0 = x * source.width / width, x1 = (x + 1) * source.width / width, y0 = y * source.height / height, y1 = (y + 1) * source.height / height;
            double sum[3] = {};
            int count = 0;
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx) {
                    std::size_t i = static_cast<std::size_t>((sy * source.width + sx) * 4);
                    for (int k = 0; k < 3; ++k) sum[k] += source.rgba[i + static_cast<std::size_t>(k)];
                    ++count;
                }
            std::size_t o = static_cast<std::size_t>((y * width + x) * 4);
            for (int k = 0; k < 3; ++k) out.rgba[o + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(sum[k] / std::max(1, count));
        }
    return out;
}

double difference(const Pixels& a, const Pixels& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    double total = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        for (int k = 0; k < 3; ++k) total += std::abs(a.rgba[i + static_cast<std::size_t>(k)] - b.rgba[i + static_cast<std::size_t>(k)]);
    return total / (a.rgba.size() / 4 * 3);
}

void keep(const Pixels& image, const std::string& name) {
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) {
        std::filesystem::create_directories(folder);
        save(image, std::filesystem::path(folder) / ("caricamento " + name + ".png"));
    }
}

void compare(const Pixels& image, const std::string& name, double limit) {
    keep(image, name);
    auto reference = golden::root() / "Tests/Golden/Loading" / (name + ".png");
    if (golden::writing()) {
        std::filesystem::create_directories(reference.parent_path());
        save(image, reference);
    }
    REQUIRE(std::filesystem::exists(reference));
    double d = difference(image, load(reference));
    INFO(name << ": differenza " << d);
    CHECK(d < limit);
}

}

TEST_CASE("Caricamento: copertina del porto", "[loading][backdrop][rendering]") {
    ld::setResourceFolder(golden::root() / "Resources");
    auto scene = harbor::build();
    CHECK(scene->parts >= 800);
    CHECK(scene->people.size() >= 10);
    auto image = ld::render::Renderer::shared().snapshot(scene->world->scene, *scene->camera, 640, 360, 4, true);
    REQUIRE(image.width == 640);
    Pixels small{image.width, image.height, image.rgba};
    compare(small, "Porto 640", 3);
    auto masterPath = golden::root() / "Resources/Backgrounds/Loading.jpg";
    REQUIRE(std::filesystem::exists(masterPath));
    CHECK(std::filesystem::file_size(masterPath) < 4 * 1024 * 1024);
    Pixels master = load(masterPath);
    CHECK(master.width == 3840);
    CHECK(master.height == 2160);
    double gap = difference(shrink(master, 640, 360), small);
    INFO("differenza master e scena attuale " << gap);
    CHECK(gap < 8);
}

TEST_CASE("Caricamento: schermata in corso, quasi finita e in errore", "[loading][player][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        golden::registry();
        ld::setResourceFolder(golden::root() / "Resources");
        struct Case {
            std::string name, gate, refusal;
        };
        for (const Case& state : {Case{"in corso", "LudisseyOpenSession", ""}, Case{"quasi finita", "LudisseyReadySession", ""}, Case{"errore", "LudisseyOpenSession", "[ServerFull] Server pieno"}}) {
            INFO(state.name);
            harness::Fixture fixture;
            auto backend = std::make_shared<GatedBackend>();
            backend->wrapped = fixture.world->registerPlayer("Ploading", "Giocatrice");
            backend->gate = state.gate;
            backend->refusal = state.refusal;
            auto store = std::make_shared<Store>(std::make_shared<PlayFab>(std::make_shared<MemoryStore>()), backend, nullptr, false);
            store->setDiscover({WorldSummary{fixture.experience, "Porto del Tramonto", "owner", "Martina", std::string("Avventura"), 12, std::nullopt, std::nullopt, std::nullopt, std::nullopt}});
            BackendJoinSessionProvider provider(fixture.world->registerPlayer("Ploading", "Giocatrice"));
            LaunchRequest::shared().joinTicket = harness::await(provider.requestSession(ExperienceId(fixture.experience), std::nullopt));
            {
                player::PlayerShell player(store);
                ui::Surface surface([&] { return player.build(); });
                auto draw = [&](int width, int height) {
                    surface.resize({static_cast<double>(width), static_cast<double>(height)}, 1);
                    ui::Canvas canvas(width, height, 1);
                    surface.render(canvas);
                    return Pixels{canvas.pixelWidth(), canvas.pixelHeight(), std::vector<std::uint8_t>(canvas.pixels(), canvas.pixels() + canvas.pixelWidth() * canvas.pixelHeight() * 4)};
                };
                draw(1400, 900);
                if (state.refusal.empty()) REQUIRE(harness::waitUntil([&] { return backend->waiting; }));
                else REQUIRE(harness::waitUntil([&] {
                    draw(1400, 900);
                    return surface.contains("Server pieno");
                }));
                harness::pumpFor(0.05);
                for (auto [width, height] : std::vector<std::pair<int, int>>{{1400, 900}, {1180, 760}}) {
                    Pixels image = draw(width, height);
                    CHECK(surface.find("player.loading.band"));
                    CHECK(surface.contains("Porto del Tramonto"));
                    CHECK(surface.contains("di Martina"));
                    if (!state.refusal.empty()) {
                        CHECK(surface.contains("Scegli un altro server oppure premi Gioca: ti mettiamo dove c’è posto."));
                        CHECK(surface.find("player.leave"));
                    } else CHECK(surface.find("player.cancel"));
                    compare(image, state.name + " " + std::to_string(width), 1.5);
                }
                backend->resume = true;
                harness::pumpFor(0.05);
            }
            LaunchRequest::shared().joinTicket.reset();
        }
    }
}
