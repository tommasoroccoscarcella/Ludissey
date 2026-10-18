#include "Interface/Components.hpp"
#include "Interface/Surface.hpp"

#include <Catch2/catch_amalgamated.hpp>

using namespace ui;
using Catch::Approx;

namespace {

Frame frameOf(Surface& s, const std::string& id) {
    auto e = s.find(id);
    REQUIRE(e.has_value());
    return e->frame;
}

}

TEST_CASE("Interfaccia: stack con figli fissi e flessibili", "[interface][layout]") {
    Surface surface([] {
        return vstack({
            hstack({rectangle(Color::black())->frameSize(40, 20)->id("a"), spacer(), rectangle(Color::black())->frameSize(60, 20)->id("b")}, 8),
            rectangle(Color::black())->frameSize(std::nullopt, 30)->id("c"),
            spacer(),
            text("Ludissey", Font{"Helvetica", 14, Weight::bold})->id("t"),
        }, 10)->padding(Insets{12, 16, 12, 16});
    });
    surface.resize({300, 200}, 2);
    Canvas canvas(300, 200, 2);
    surface.render(canvas);
    CHECK(frameOf(surface, "a").x == 16);
    CHECK(frameOf(surface, "a").y == 12);
    CHECK(frameOf(surface, "b").maxX() == 284);
    CHECK(frameOf(surface, "c").width == 268);
    CHECK(frameOf(surface, "c").y == 42);
    Frame t = frameOf(surface, "t");
    CHECK(t.maxY() == Approx(188).margin(0.5));
    CHECK(t.midX() == Approx(150).margin(0.5));
    CHECK(t.width > 40);
}

TEST_CASE("Flexible frames and fixed sizes follow proposals", "[interface][layout]") {
    Surface surface([] {
        return hstack({
            text("Sinistra")->fillWidth(Alignment::leading())->id("l"),
            text("Destra")->fixed()->id("r"),
        }, 0);
    });
    surface.resize({400, 40}, 1);
    Canvas canvas(400, 40, 1);
    surface.render(canvas);
    Frame l = frameOf(surface, "l"), r = frameOf(surface, "r");
    CHECK(r.maxX() == Approx(400));
    CHECK(l.x == 0);
    CHECK(l.width == Approx(400 - r.width));
}

TEST_CASE("Taps, hover and drags reach the right region", "[interface][events]") {
    int taps = 0;
    bool hovering = false;
    Point moved;
    Surface surface([&] {
        return zstack({
            rectangle(Color::hex("F2F3F5"))->onDrag("fondo", [&](const DragEvent& e) { moved = e.translation; }),
            rectangle(Color::hex("6730EE"))->frameSize(100, 32)->onTap("pulsante", [&] { ++taps; })->onHover("pulsante", [&](bool h) { hovering = h; }),
        });
    });
    surface.resize({300, 200}, 1);
    Canvas canvas(300, 200, 1);
    surface.render(canvas);
    surface.mouseMoved({150, 100});
    CHECK(hovering);
    CHECK(surface.hovered("pulsante"));
    surface.mouseDown({150, 100});
    CHECK(surface.pressed("pulsante"));
    surface.mouseUp({150, 100});
    CHECK(taps == 1);
    surface.mouseDown({150, 100});
    surface.mouseUp({10, 10});
    CHECK(taps == 1);
    surface.mouseDown({10, 10});
    surface.mouseDragged({40, 30});
    surface.mouseUp({40, 30});
    CHECK(moved == Point{30, 20});
    CHECK(surface.tap("pulsante"));
    CHECK(taps == 2);
    surface.mouseMoved({5, 5});
    CHECK_FALSE(hovering);
    Color c = canvas.pixel(150, 100);
    CHECK(c.r == Approx(0x67 / 255.0).margin(0.01));
    CHECK(c.b == Approx(0xEE / 255.0).margin(0.01));
}

TEST_CASE("Scroll views clamp offsets and clip content", "[interface][scroll]") {
    Surface surface([] {
        std::vector<NodePtr> rows;
        for (int i = 0; i < 20; ++i) rows.push_back(rectangle(Color::black())->frameSize(std::nullopt, 40)->id("riga" + std::to_string(i)));
        return scroll("elenco", vstack(std::move(rows), 0))->frameSize(200, 100);
    });
    surface.resize({200, 100}, 1);
    Canvas canvas(200, 100, 1);
    surface.render(canvas);
    CHECK(frameOf(surface, "riga0").y == 0);
    CHECK(surface.scrollWheel({100, 50}, 0, -60));
    surface.render(canvas);
    CHECK(frameOf(surface, "riga0").height == 0);
    CHECK(frameOf(surface, "riga2").y == 20);
    surface.scrollWheel({100, 50}, 0, -10000);
    surface.render(canvas);
    CHECK(surface.scrollOffset("elenco").y == 700);
    CHECK_FALSE(surface.scrollWheel({100, 50}, 0, -10));
}

TEST_CASE("Interfaccia: campi nativi e viewport", "[interface][native]") {
    Surface surface([] {
        NativeField f;
        f.id = "ricerca";
        f.prompt = "Cerca";
        return vstack({textField(f)->frameSize(std::nullopt, 32), viewport("mondo")}, 0);
    });
    surface.resize({320, 240}, 2);
    Canvas canvas(320, 240, 2);
    surface.render(canvas);
    REQUIRE(surface.fields().size() == 1);
    CHECK(surface.fields()[0].frame.midY() == Approx(16));
    REQUIRE(surface.viewports().size() == 1);
    CHECK(surface.viewports()[0].frame == Frame{0, 32, 320, 208});
}

TEST_CASE("Componenti: lucido di iPhone OS 3", "[interface][components]") {
    int chosen = 0;
    bool on = false;
    Surface surface([&] {
        return vstack({
            hstack({button("annulla", "Annulla", [] {}), button("esci", "Esci", [] {}, ButtonRole::destructive), button("gioca", "Gioca", [] {}, ButtonRole::primary)->disabled()}, 10),
            segments("modo", {"Uno", "Due", "Tre"}, chosen, [&](int i) { chosen = i; }),
            toggle("interruttore", on, [&](bool v) { on = v; }),
            hstack({icon("ld-icon-service-players"), icon("ld-icon-studio-move", 16), icon("sconosciuto", 20, "?"), logo(20)->foreground(theme::colors::brand)}, 8),
        }, 12, HAlign::leading)->padding(20)->fill(Alignment::topLeading())->background(rectangle(theme::colors::surface));
    });
    surface.resize({420, 260}, 2);
    Canvas canvas(420, 260, 2);
    surface.render(canvas);
    Frame cancel = surface.find("annulla")->frame;
    CHECK(cancel.height == theme::metrics::button);
    CHECK(cancel.x == 20);
    Color upper = canvas.pixel(cancel.x + 8, cancel.y + 6), lower = canvas.pixel(cancel.x + 8, cancel.maxY() - 6);
    CHECK(upper.r > lower.r);
    CHECK(canvas.pixel(cancel.midX(), cancel.maxY() - 0.5).r == Approx(theme::gloss::white.border.r).margin(0.03));
    Frame exit = surface.find("esci")->frame;
    CHECK(exit.x == Approx(cancel.maxX() + 10));
    Color red = canvas.pixel(exit.x + 6, exit.maxY() - 8);
    CHECK(red.r > red.g + 0.4);
    surface.mouseDown({exit.midX(), exit.midY()});
    surface.render(canvas);
    CHECK(canvas.pixel(exit.midX(), exit.y + 2).r < red.r + 0.3);
    surface.mouseUp({exit.midX(), exit.midY()});
    CHECK_FALSE(surface.tap("gioca"));
    Frame play = surface.find("gioca")->frame;
    Color faded = canvas.pixel(play.x + 6, play.maxY() - 8);
    CHECK(faded.r > theme::gloss::purple.bottom.r + 0.15);
    CHECK(surface.tap("modo.2"));
    surface.render(canvas);
    CHECK(chosen == 2);
    Frame third = surface.find("modo.2")->frame, first = surface.find("modo.0")->frame;
    CHECK(third.height == theme::metrics::segmented);
    Color picked = canvas.pixel(third.midX(), third.maxY() - 6), idle = canvas.pixel(first.midX(), first.maxY() - 6);
    CHECK(picked.b > picked.r + 0.3);
    CHECK(idle.r > 0.8);
    Frame toggle = surface.find("interruttore")->frame;
    CHECK(toggle.width == theme::metrics::toggleWidth);
    CHECK(toggle.height == theme::metrics::toggleHeight);
    CHECK(surface.tap("interruttore"));
    CHECK(on);
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) canvas.writePng(std::filesystem::path(folder) / "componenti.png");
}

TEST_CASE("Dialogs block the content behind them", "[interface][components]") {
    int taps = 0, confirmed = 0;
    Surface surface([&] {
        return zstack({
            button("sotto", "Sotto", [&] { ++taps; })->fill(),
            dialog("Esci dall’account?", "Le modifiche all’avatar non salvate andranno perse.", {{"Annulla", ButtonRole::secondary, [] {}}, {"Esci", ButtonRole::destructive, [&] { ++confirmed; }}}),
        });
    });
    surface.resize({860, 760}, 2);
    Canvas canvas(860, 760, 2);
    surface.render(canvas);
    surface.mouseDown({20, 20});
    surface.mouseUp({20, 20});
    CHECK(taps == 0);
    Frame exit = surface.find("dialog.button.1")->frame;
    surface.mouseDown({exit.midX(), exit.midY()});
    surface.mouseUp({exit.midX(), exit.midY()});
    CHECK(confirmed == 1);
    Frame stay = surface.find("dialog.button.0")->frame;
    CHECK(exit.maxX() == Approx(430 + theme::metrics::alertWidth / 2 - theme::spacing::s6));
    CHECK(exit.height == theme::metrics::alertButton);
    CHECK(stay.maxX() < exit.x);
    CHECK(stay.y == exit.y);
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) canvas.writePng(std::filesystem::path(folder) / "dialogo.png");
}

TEST_CASE("Font: pesi di Helvetica Neue", "[interface][font]") {
    std::string family(tokens::font::family);
    CHECK(family == "Helvetica Neue");
    CHECK(postScriptName({family, 16, Weight::light}) == "HelveticaNeue-Light");
    CHECK(postScriptName({family, 16, Weight::regular}) == "HelveticaNeue");
    CHECK(postScriptName({family, 16, Weight::bold}) == "HelveticaNeue-Bold");
    CHECK(theme::type::pageTitle.weight == Weight::light);
    CHECK(theme::type::sectionTitle.weight == Weight::bold);
    CHECK(theme::type::heading.weight == Weight::bold);
    CHECK(theme::type::label.weight == Weight::bold);
    CHECK(theme::type::body.size == tokens::font::body);
    theme::applyScale(theme::Scale::studio);
    CHECK(theme::type::body.size == tokens::studioFont::body);
    CHECK(theme::type::pageTitle.weight == Weight::bold);
    CHECK(postScriptName(theme::type::body) == "HelveticaNeue");
    theme::applyScale(theme::Scale::client);
    CHECK(theme::type::body.size == tokens::font::body);
}

TEST_CASE("Componenti: controlli di iPhone OS 3", "[interface][components]") {
    int tab = 0;
    bool on = true;
    Surface surface([&] {
        NativeField field;
        field.id = "campo";
        field.prompt = "Nome utente";
        return vstack({
            hstack({button("primario", "Salva", [] {}, ButtonRole::primary), button("verde", "Accedi", [] {}, ButtonRole::success), button("spento", "Entra", [] {})->disabled(), linkButton("tutte", "Vedi tutte", [] {})}, 8),
            playButton("gioca", "Gioca", [] {})->frameSize(240, std::nullopt),
            styledField(field)->frameSize(240, std::nullopt),
            toggle("interruttore", on, [&](bool v) { on = v; }),
            pageTabs("schede", {"Informazioni", "Server"}, tab, [&](int i) { tab = i; }),
            card(text("Card")->padding(theme::spacing::s6), "card")->id("card"),
            emptyState("ld-icon-app-friends", "Nessun amico online adesso.")->id("vuoto"),
            hstack({button("stretto", "Applica", [] {})}, 0)->compact(),
        }, 12, HAlign::leading)->padding(20)->fill(Alignment::topLeading())->background(rectangle(theme::colors::canvas));
    });
    surface.resize({420, 520}, 2);
    Canvas canvas(420, 520, 2);
    surface.render(canvas);
    Frame primary = surface.find("primario")->frame;
    CHECK(primary.height == theme::metrics::button);
    Color top = canvas.pixel(primary.midX(), primary.y + 4), bottom = canvas.pixel(primary.midX(), primary.maxY() - 4);
    CHECK(top.b > bottom.b);
    CHECK(bottom.b > bottom.g + 0.3);
    Frame green = surface.find("verde")->frame;
    Color leaf = canvas.pixel(green.x + 4, green.maxY() - 6);
    CHECK(leaf.g > leaf.r + 0.2);
    Frame off = surface.find("spento")->frame;
    CHECK(canvas.pixel(off.x + 4, off.maxY() - 6).r > theme::gloss::white.bottom.r - 0.02);
    CHECK(surface.find("tutte")->frame.height == theme::metrics::buttonSmall);
    Frame play = surface.find("gioca")->frame;
    CHECK(play.height == theme::metrics::playButton);
    CHECK(play.width == 240);
    Color grass = canvas.pixel(play.x + 6, play.maxY() - 8);
    CHECK(grass.g > grass.r + 0.2);
    REQUIRE(surface.fields().size() == 1);
    Frame field = surface.fields()[0].full;
    double edge = field.midY() - theme::metrics::textField / 2;
    CHECK(canvas.pixel(field.midX(), edge + 0.5).r == Approx(theme::colors::border.r).margin(0.04));
    surface.setFocusedField("campo");
    surface.render(canvas);
    CHECK(canvas.pixel(field.midX(), edge + 0.5).b == Approx(theme::colors::focus.b).margin(0.04));
    CHECK(canvas.pixel(field.midX(), edge - 2.5).r == Approx(theme::colors::canvas.r).margin(0.01));
    Frame toggle = surface.find("interruttore")->frame;
    CHECK(toggle.height == theme::metrics::toggleHeight);
    Color lit = canvas.pixel(toggle.x + 10, toggle.midY());
    CHECK(lit.b > lit.r + 0.3);
    CHECK(surface.tap("interruttore"));
    CHECK_FALSE(on);
    surface.render(canvas);
    CHECK(canvas.pixel(toggle.x + 10, toggle.midY()).r > 0.85);
    Frame first = surface.find("schede.0")->frame, second = surface.find("schede.1")->frame;
    CHECK(first.height == theme::metrics::segmented);
    CHECK(second.x > first.maxX());
    Color chosen = canvas.pixel(first.midX(), first.maxY() - 6);
    CHECK(chosen.b > chosen.r + 0.3);
    CHECK(surface.tap("schede.1"));
    CHECK(tab == 1);
    Frame box = surface.find("card")->frame;
    CHECK(canvas.pixel(box.x + 0.5, box.midY()).r == Approx(theme::colors::border.r).margin(0.04));
    CHECK(canvas.pixel(box.x + 4, box.midY()).r == Approx(1).margin(0.01));
    CHECK(surface.contains("Nessun amico online adesso."));
    CHECK(surface.find("stretto")->frame.height == theme::metrics::buttonSmall);
    if (const char* folder = std::getenv("LUDISSEY_SHOTS")) canvas.writePng(std::filesystem::path(folder) / "componenti iphone.png");
}

TEST_CASE("Ombre: un contenitore proietta una sola ombra", "[interface][components][shadow]") {
    auto box = [](bool shaded) {
        NodePtr content = vstack({text("Impostazioni esperienza", theme::type::body), text("Nome dell’esperienza", theme::type::small)}, 4, HAlign::leading)->padding(12)->background(rectangle(theme::colors::surface));
        if (shaded) content = content->shadow(Color::black(0.4), 0, 3, 0);
        return content->padding(20)->fill(Alignment::topLeading())->background(rectangle(theme::colors::canvas));
    };
    auto render = [&](bool shaded) {
        Surface surface([&] { return box(shaded); });
        surface.resize({320, 140}, 2);
        Canvas canvas(320, 140, 2);
        surface.render(canvas);
        return std::vector<std::uint8_t>(canvas.pixels(), canvas.pixels() + static_cast<std::size_t>(canvas.pixelWidth() * canvas.pixelHeight() * 4));
    };
    auto plainPixels = render(false), shadedPixels = render(true);
    int inside = 0, outside = 0;
    for (int y = 0; y < 280; ++y)
        for (int x = 0; x < 640; ++x) {
            std::size_t i = static_cast<std::size_t>((y * 640 + x) * 4);
            bool differs = std::abs(plainPixels[i] - shadedPixels[i]) > 2 || std::abs(plainPixels[i + 1] - shadedPixels[i + 1]) > 2;
            bool within = x >= 40 && x < 600 && y >= 40 && y < 120;
            if (differs && within) ++inside;
            if (differs && !within) ++outside;
        }
    CHECK(inside == 0);
    CHECK(outside > 0);
}
