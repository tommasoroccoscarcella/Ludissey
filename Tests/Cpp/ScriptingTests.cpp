#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Gui/GuiLayout.hpp"
#include "Engine/Scripting/ScriptEngine.hpp"
#include "Engine/Scripting/WorldSession.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"
#include "Tests/Support/Timing.hpp"

#include <algorithm>
#include <chrono>

using namespace ld;

namespace {

InstanceRecord record(const std::string& className, const std::string& name, const std::string& parent, std::optional<std::string> source = std::nullopt, bool disabled = false) {
    InstanceRecord r;
    r.id = makeUuid();
    r.className = className;
    r.name = name;
    r.parent = parent;
    if (source) {
        r.properties["Source"] = PropertyValue::string(*source);
        r.properties["Disabled"] = PropertyValue::boolean(disabled);
    }
    return r;
}

WorldBlock base() {
    WorldBlock block;
    block.id = makeUuid();
    block.name = "Base";
    block.sizeX = 4;
    block.sizeY = 1;
    block.sizeZ = 4;
    block.y = 0.5;
    return block;
}

Experience experience(std::vector<InstanceRecord> records, std::vector<WorldBlock> blocks = {base()}) {
    Experience world;
    world.id = makeUuid();
    world.blocks = std::move(blocks);
    world.instances = std::move(records);
    return world;
}

bool contains(const std::vector<std::string>& lines, const std::string& text) { return std::find(lines.begin(), lines.end(), text) != lines.end(); }

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l + " | ";
    return out;
}

struct Harness {
    std::vector<OutputLine> lines;
    DataModel model;
    ScriptEngine engine;
    Harness() : model(experience({})), engine(model, [this](const OutputLine& l) { lines.push_back(l); }) {}
    std::vector<std::string> run(const std::string& source, const std::string& label = "Workspace.Prova") {
        lines.clear();
        engine.execute(source, ScriptContext::server, label);
        for (int i = 0; i < 30 && engine.running() > 0; ++i) engine.step(1.0 / 30);
        std::vector<std::string> out;
        for (const auto& l : lines) out.push_back(l.text);
        return out;
    }
};

}

TEST_CASE("Registro storico 2017 e tipi Lim del motore", "[scripting][registry]") {
    const ClassRegistry& registry = golden::registry();
    CHECK(registry.dataset() == 2017);
    CHECK(registry.descriptors().size() == 383);
    CHECK(registry.enums().size() == 171);
    for (const char* future : {"UICorner", "ProximityPrompt", "ViewportFrame", "ValueBase", "UIStroke"}) CHECK_FALSE(registry.find(future));
    const auto& types = EngineTypes::shared();
    CHECK(types.registry().property("Part", "position"));
    CHECK(types.registry().property("Part", "transform"));
    CHECK(types.registry().property("Frame", "backgroundColor"));
    CHECK(types.registry().method("Instance", "findFirstChild"));
    CHECK(types.registry().event("BasePart", "touched"));
    CHECK(types.registry().findEnum("Material"));
    CHECK(*types.property("Frame", "backgroundColor") == "BackgroundColor3");
    CHECK(limMemberName("MouseButton1Click") == "mouseButton1Click");
    CHECK(limMemberName("UserId") == "userId");
    CHECK(limMemberName("HTTPEnabled") == "httpEnabled");
    CHECK(limMemberName("CFrame") == "transform");
}

TEST_CASE("Lim nel motore: istanze, eventi e sandbox", "[scripting][lim]") {
    golden::registry();
    Harness h;
    CHECK(h.run("print(1, 2, 3)") == std::vector<std::string>{"1 2 3"});
    CHECK(h.run("let p = Part(parent: World)\np.name = \"Nuova\"\np.size = Vec3(2, 3, 4)\nprint(p.parent == World, World.findFirstChild(\"Nuova\") == p, p.size.y, p.className)") ==
          std::vector<std::string>{"true true 3 Part"});
    auto future = h.run("let x = ProximityPrompt()");
    REQUIRE_FALSE(future.empty());
    CHECK(future[0].find("ProximityPrompt") != std::string::npos);
    CHECK(h.lines[0].error());
    CHECK(h.run("print((Vec3(1, 2, 3) + Vec3(1, 1, 1)).x, Vec3(3, 4, 0).length, Transform(Vec3(1, 2, 3)).apply(Vec3(1, 0, 0)).x)") == std::vector<std::string>{"2 5 2"});
    CHECK(h.run("print(Color(1, 0, 0).r, Dim2(0.5, 10, 0, 20).xOffset, typeOf(Vec3(0, 0, 0)), World.className, Material.Wood.name)") ==
          std::vector<std::string>{"1 10 Vec3 Workspace Wood"});
    auto missing = h.run("print(World.inesistente)");
    REQUIRE_FALSE(missing.empty());
    CHECK(missing[0].find("non è un membro di Workspace") != std::string::npos);
    CHECK(h.run("let f = Folder(parent: World)\nlet v = IntValue(parent: f)\nv.value = 7\nprint(f.getChildren().count, v.value, v.isDescendantOf(World))\nv.destroy()\nprint(v.parent)") ==
          std::vector<std::string>{"1 7 true", "none"});
    CHECK(h.run("let p = Part(parent: World)\non p.changed(name) {\n    print(\"cambiato\", name)\n}\np.transparency = 0.5") == std::vector<std::string>{"cambiato transparency"});
    CHECK(h.run("let e = BindableEvent()\non e.event(v) {\n    print(\"ricevuto\", v)\n}\ne.fire(42)") == std::vector<std::string>{"ricevuto 42"});
    CHECK(h.run("print(\"prima\")\nawait Task.wait(0.2)\nprint(\"dopo\")") == std::vector<std::string>{"prima", "dopo"});
    auto broken = h.run("let a = 1\nerror(\"rotto\")", "Workspace.Guasto");
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].rfind("Workspace.Guasto:2:", 0) == 0);
    CHECK(broken[0].find("rotto") != std::string::npos);
    CHECK(h.lines[0].error());
    auto syntax = h.run("let x = ", "Workspace.Sintassi");
    REQUIRE_FALSE(syntax.empty());
    CHECK(syntax[0].rfind("Workspace.Sintassi:1:", 0) == 0);
    CHECK(h.lines[0].error());
    for (const char* name : {"io", "os", "loadstring", "dofile", "require", "File", "Process", "Shell", "debug", "package"}) {
        auto sandbox = h.run(std::string("print(") + name + ")");
        REQUIRE_FALSE(sandbox.empty());
        CHECK(sandbox[0].find("Nome sconosciuto") != std::string::npos);
    }
    auto started = std::chrono::steady_clock::now();
    double cpu = support::timing::threadSeconds();
    auto timeout = h.run("while true {\n}", "Workspace.Infinito");
    REQUIRE_FALSE(timeout.empty());
    CHECK(timeout[0].find("Tempo di esecuzione esaurito") != std::string::npos);
    UNINSTRUMENTED_CHECK(support::timing::threadSeconds() - cpu < 2);
    TIMED_CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < 2);
    CHECK(h.run("print(\"vivo\")") == std::vector<std::string>{"vivo"});
    auto memory = h.run("var s = \"x\"\nwhile true {\n    s = s + s\n}");
    REQUIRE_FALSE(memory.empty());
    CHECK((memory[0].find("Memoria esaurita") != std::string::npos || memory[0].find("Tempo") != std::string::npos));
}

TEST_CASE("Lim nel motore: tipi e contesti", "[scripting][lim]") {
    golden::registry();
    Harness h;
    auto typed = h.run("let p = Part()\np.anchored = \"sì\"");
    REQUIRE_FALSE(typed.empty());
    CHECK(typed[0].find("Bool") != std::string::npos);
    auto context = h.run("print(Input.isKeyDown(KeyCode.W))");
    REQUIRE_FALSE(context.empty());
    CHECK(context[0].find("client") != std::string::npos);
    CHECK(h.run("let p = Part(parent: World)\np.material = Material.Wood\nprint(p.material.name, p.color.hex)") == std::vector<std::string>{"Wood " + std::string(rules::part::color)});
    CHECK(h.run("let hit = Physics.raycast(Vec3(0, 10, 0), Vec3(0, -20, 0))\nprint(hit != none)") == std::vector<std::string>{"true"});
    CHECK(h.run("let p = Part(parent: World, name: \"Etichettata\")\nTags.add(p, \"lava\")\nprint(Tags.has(p, \"lava\"), Tags.tagged(\"lava\").count)") == std::vector<std::string>{"true 1"});
    CHECK(h.run("let p = Part(parent: World)\nlet t = Tween.create(p, [\"transparency\": 1], 0.1)\nt.play()\nawait Task.wait(0.3)\nprint(p.transparency)") == std::vector<std::string>{"1"});
    auto remote = h.run("let r = RemoteEvent(parent: Storage)\nr.broadcast(fn () { })");
    REQUIRE_FALSE(remote.empty());
    INFO(remote[0]);
    CHECK(remote[0].find("Si possono inviare solo") != std::string::npos);
}

TEST_CASE("Lim: esempi con la API World", "[scripting][lim][creator]") {
    golden::registry();
    Harness h;
    auto output = h.run(R"LIM(let part = Part()
part.name = "Platform"
part.position = Vec3(0, 5, 0)
part.size = Vec3(12, 1, 12)
part.color = Color.rgb(0.2, 0.5, 1.0)
part.anchored = true
World.add(part)
print(World.find("Platform") == part)
)LIM");
    REQUIRE(output == std::vector<std::string>{"true"});
    auto part = h.model.find(h.model.workspace(), "Platform");
    REQUIRE(part);
    REQUIRE(part->block);
    CHECK(part->block->y == 5);
    CHECK(part->block->sizeX == 12);
    CHECK(part->block->sizeY == 1);
    CHECK(part->block->anchored);
    auto cyclic = h.run("let f = Folder(parent: World)\nf.add(f)");
    REQUIRE(cyclic.size() == 1);
    CHECK(cyclic.front().find("genitore di se stesso") != std::string::npos);
    CHECK(h.run("let p = Part()\np.destroy()\nWorld.add(p)").size() == 1);
    CHECK(h.lines.front().error());
    CHECK(h.run("let b = TextButton()\non b.clicked { print(\"clic\") }\nprint(\"pronto\")") == std::vector<std::string>{"pronto"});
    CHECK(h.run("on Players.joined(player) { print(player.name) }\nprint(\"collegato\")") == std::vector<std::string>{"collegato"});
}

TEST_CASE("DataModel: genitori estranei o distrutti", "[scripting][ownership]") {
    golden::registry();
    DataModel model(Experience::blank()), other(Experience::blank());
    auto child = model.create("Folder");
    auto parent = model.create("Folder");
    REQUIRE(child);
    REQUIRE(parent);
    REQUIRE(model.setParent(*child, model.workspace()));
    CHECK_FALSE(model.setParent(*child, other.workspace()));
    CHECK_FALSE(other.setParent(*child, other.workspace()));
    CHECK((*child)->parent() == model.workspace());
    model.destroy(*parent);
    CHECK_FALSE(model.setParent(*child, *parent));
    CHECK_FALSE(model.setParent(nullptr, model.workspace()));
    CHECK((*child)->parent() == model.workspace());
    CHECK(other.find((*child)->id) == nullptr);
}

TEST_CASE("WorldSession: contesti, moduli e input", "[scripting][session]") {
    golden::registry();
    std::vector<std::string> lines;
    auto module = record("ModuleScript", "Contatore", "ReplicatedStorage", "var n = 0\nfn next() -> Integer {\n    n += 1\n    return n\n}\n");
    auto loop = record("ModuleScript", "Anello", "ReplicatedStorage", "import Anello\n");
    auto screen = record("ScreenGui", "Interfaccia", "StarterGui");
    auto tool = record("Tool", "Spada", "StarterPack");
    auto off = record("Script", "Spento", "ServerScriptService", "print(\"acceso\")\n", true);
    WorldBlock part = base();
    std::vector<InstanceRecord> records = {
        module, loop, screen, tool, off,
        record("Script", "Tocco", part.id, "let parte = script.parent\nif parte is BasePart {\n    on parte.touched(hit) {\n        print(\"tocco\", hit.name, Players.getPlayerFromCharacter(hit.parent) != none)\n    }\n}\n"),
        record("Script", "PrimoModulo", "ServerScriptService", "import Contatore\nprint(\"modulo\", Contatore.next())\n"),
        record("Script", "SecondoModulo", "ServerScriptService", "import Contatore\nprint(\"modulo\", Contatore.next())\n"),
        record("Script", "TerzoModulo", "ServerScriptService", "import Anello\n"),
        record("RemoteEvent", "Remoto", "ReplicatedStorage"),
        record("Script", "Server", "ServerScriptService",
               "let remoto = Network.event(\"Remoto\")\nif remoto != none {\n    on remoto.onServerEvent(player, x) {\n        print(\"dal client\", player.name, x)\n    }\n}\nawait Task.wait(0.1)\nlet spento = ServerScripts.findFirstChild(\"Spento\")\nif spento is Script {\n    spento.disabled = false\n}\n"),
        record("Script", "Magazzino", "ServerStorage", "print(\"non deve partire\")\n"),
        record("LocalScript", "LocaleInWorkspace", "Workspace", "print(\"non deve partire\")\n"),
        record("LocalScript", "Giocatore", "StarterPlayer.StarterPlayerScripts",
               "print(\"locale\", Players.localPlayer?.name ?? \"\")\nNetwork.event(\"Remoto\")?.fireServer(\"ciao\")\non Input.inputBegan(input, processed) {\n    print(\"tasto\", input.keyCode.name)\n}\n"),
        record("LocalScript", "Gui", screen.id, "print(\"gui in\", script.parent?.parent?.name ?? \"\")\n"),
        record("LocalScript", "Arma", tool.id,
               "let attrezzo = script.parent\nprint(\"attrezzo in\", attrezzo?.parent?.name ?? \"\")\nif attrezzo is Tool {\n    on attrezzo.equipped(mouse) {\n        print(\"impugnato\", attrezzo.parent?.name ?? \"\")\n    }\n    on attrezzo.activated() {\n        print(\"attivato\")\n    }\n}\n"),
        record("Script", "Personaggio", "StarterPlayer.StarterCharacterScripts",
               "let corpo = script.parent\nif corpo != none {\n    let h = await corpo.waitForChild(\"Humanoid\")\n    if h is Humanoid {\n        h.walkSpeed = 32\n        on h.died() {\n            print(\"morto\")\n        }\n        await Task.wait(0.3)\n        let radice = corpo.findFirstChild(\"HumanoidRootPart\")\n        if radice is BasePart {\n            radice.transform = Transform(Vec3(5, 2, 5))\n        }\n        await Task.wait(0.3)\n        h.takeDamage(150)\n    }\n}\n")};
    WorldSession session(experience(records, {part}), WorldSession::Mode::play, "Tester", 0, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.physics = false;
    session.start({0, 1, 0});
    std::optional<Vec3> teleported;
    Vec3 position{0, 1, 0};
    for (int frame = 0; frame < 50; ++frame) {
        session.place(position.x, position.y, position.z, 0);
        if (frame == 5) session.key(13, true);
        session.advance(1.0 / 30);
        if (auto point = session.teleport()) {
            teleported = point;
            position = *point;
        }
        if (frame == 6) CHECK(session.speedFactor() == 2);
    }
    INFO(joined(lines));
    REQUIRE(teleported);
    CHECK(std::fabs(teleported->x - 5) < 0.001);
    CHECK(std::fabs(teleported->z - 5) < 0.001);
    for (const char* expected : {"modulo 1", "modulo 2", "dal client Tester ciao", "locale Tester", "gui in PlayerGui", "attrezzo in Backpack", "tocco Left Leg true", "acceso", "tasto W", "morto"})
        CHECK(contains(lines, expected));
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("ricorsivo") != std::string::npos; }));
    CHECK_FALSE(contains(lines, "non deve partire"));
    CHECK(session.dead());
    session.spawn({0, 1, 0});
    session.advance(1.0 / 30);
    CHECK_FALSE(session.dead());
    CHECK(session.health() == 100);
    CHECK(session.character()->child("Personaggio"));
    auto tools = session.tools();
    REQUIRE(tools.size() == 1);
    CHECK(tools[0].name == "Spada");
    CHECK_FALSE(tools[0].equipped);
    session.equip(0);
    session.advance(1.0 / 30);
    session.activateTool();
    session.advance(1.0 / 30);
    CHECK(contains(lines, "impugnato Tester"));
    CHECK(contains(lines, "attivato"));
    CHECK(session.tools()[0].equipped);
    session.stop();
    std::vector<std::string> solo;
    WorldSession run(experience(records, {part}), WorldSession::Mode::run, "Player", 0, false, [&](const OutputLine& l) { solo.push_back(l.text); });
    run.start({0, 1, 0});
    for (int i = 0; i < 5; ++i) run.advance(1.0 / 30);
    CHECK(contains(solo, "modulo 1"));
    CHECK_FALSE(std::any_of(solo.begin(), solo.end(), [](const std::string& l) { return l.rfind("locale", 0) == 0; }));
    CHECK_FALSE(run.player());
    run.stop();
}

TEST_CASE("GUI: layout, visibilità e clic dal renderer", "[scripting][gui]") {
    const ClassRegistry& registry = golden::registry();
    auto node = [&](const std::string& className, const std::string& name, PropertyMap properties, std::vector<GuiNode> children = {}) {
        PropertyMap merged = registry.defaults(className);
        for (auto& [k, v] : properties) merged[k] = v;
        return GuiNode{name, className, name, merged, std::move(children)};
    };
    auto centered = node("Frame", "Centro", {{"Position", PropertyValue::udim2(0.5, 0, 0.5, 0)}, {"Size", PropertyValue::udim2(0, 200, 0, 100)}, {"AnchorPoint", PropertyValue::vector2(0.5, 0.5)}});
    auto list = node("Frame", "Lista", {{"Position", PropertyValue::udim2(0, 10, 0, 10)}, {"Size", PropertyValue::udim2(0, 100, 0, 300)}},
                     {node("UIListLayout", "Layout", {{"Padding", PropertyValue::udim(0, 5)}, {"SortOrder", PropertyValue::enumItem("LayoutOrder")}}),
                      node("TextLabel", "B", {{"Size", PropertyValue::udim2(1, 0, 0, 20)}, {"LayoutOrder", PropertyValue::number(2)}}),
                      node("TextLabel", "A", {{"Size", PropertyValue::udim2(1, 0, 0, 30)}, {"LayoutOrder", PropertyValue::number(1)}})});
    auto hidden = node("Frame", "Nascosto", {{"Visible", PropertyValue::boolean(false)}}, {node("TextLabel", "Dentro", {})});
    auto screen = node("ScreenGui", "Schermo", {}, {centered, list, hidden});
    auto off = node("ScreenGui", "Spento", {{"Enabled", PropertyValue::boolean(false)}}, {node("Frame", "Mai", {})});
    auto elements = gui_layout::resolve({screen, off}, 800, 600);
    std::map<std::string, Rect> frames;
    for (const auto& e : elements) frames[e.id] = e.frame;
    CHECK(frames["Centro"] == Rect::make(300, 250, 200, 100));
    CHECK(frames["A"] == Rect::make(10, 10, 100, 30));
    CHECK(frames["B"] == Rect::make(10, 45, 100, 20));
    CHECK_FALSE(frames.count("Nascosto"));
    CHECK_FALSE(frames.count("Dentro"));
    CHECK_FALSE(frames.count("Mai"));
    std::vector<std::string> lines;
    auto menu = record("ScreenGui", "Menu", "StarterGui");
    auto button = record("TextButton", "Pulsante", menu.id);
    auto handler = record("LocalScript", "Clic", button.id, "let pulsante = script.parent\nif pulsante is TextButton {\n    on pulsante.mouseButton1Click() {\n        pulsante.text = \"premuto\"\n        print(\"clic\")\n    }\n}\n");
    WorldSession session(experience({menu, button, handler}), WorldSession::Mode::play, "Tester", 0, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.start({0, 1, 0});
    session.advance(1.0 / 30);
    auto shown = session.gui();
    REQUIRE_FALSE(shown.empty());
    auto found = std::find_if(shown[0].children.begin(), shown[0].children.end(), [](const GuiNode& n) { return n.className == "TextButton"; });
    REQUIRE(found != shown[0].children.end());
    session.activate(found->id);
    session.advance(1.0 / 30);
    INFO(joined(lines));
    CHECK(contains(lines, "clic"));
    CHECK(session.gui()[0].children[0].text("Text") == "premuto");
    session.stop();
}

TEST_CASE("Parità: layout GUI", "[parity][gui]") {
    Json g = golden::load("gui");
    const ClassRegistry& registry = golden::registry();
    auto node = [&](const std::string& id, const std::string& className, PropertyMap properties, std::vector<GuiNode> children = {}) {
        PropertyMap merged = registry.defaults(className);
        for (auto& [k, v] : properties) merged[k] = v;
        return GuiNode{id, className, id, merged, std::move(children)};
    };
    using P = PropertyValue;
    std::vector<GuiNode> screens = {
        node("screen", "ScreenGui", {{"DisplayOrder", P::number(1)}},
             {node("bar", "Frame", {{"Size", P::udim2(1, 0, 0, 40)}, {"BackgroundColor3", P::color3("202020")}},
                   {node("pad", "UIPadding", {{"PaddingLeft", P::udim(0, 8)}, {"PaddingTop", P::udim(0, 4)}}),
                    node("title", "TextLabel", {{"Size", P::udim2(0.5, 0, 1, 0)}, {"Text", P::string("Ciao")}, {"TextXAlignment", P::enumItem("Left")}}),
                    node("button", "TextButton", {{"Size", P::udim2(0, 80, 0, 30)}, {"Position", P::udim2(1, -10, 0.5, 0)}, {"AnchorPoint", P::vector2(1, 0.5)}, {"Text", P::string("Gioca")}})}),
              node("list", "Frame", {{"Size", P::udim2(0, 200, 0, 300)}, {"Position", P::udim2(0, 10, 0, 50)}, {"ClipsDescendants", P::boolean(true)}},
                   {node("layout", "UIListLayout", {{"Padding", P::udim(0, 5)}, {"SortOrder", P::enumItem("LayoutOrder")}, {"HorizontalAlignment", P::enumItem("Center")}}),
                    node("b", "TextLabel", {{"Size", P::udim2(1, -20, 0, 30)}, {"LayoutOrder", P::number(2)}}), node("a", "TextLabel", {{"Size", P::udim2(0.5, 0, 0, 30)}, {"LayoutOrder", P::number(1)}}),
                    node("hidden", "TextLabel", {{"Size", P::udim2(1, 0, 0, 30)}, {"Visible", P::boolean(false)}})}),
              node("grid", "Frame", {{"Size", P::udim2(0, 220, 0, 120)}, {"Position", P::udim2(0.5, 0, 1, -130)}},
                   {node("g", "UIGridLayout", {{"CellSize", P::udim2(0, 60, 0, 50)}, {"CellPadding", P::udim2(0, 5, 0, 5)}}), node("c1", "ImageLabel", {}), node("c2", "ImageButton", {}),
                    node("c3", "TextBox", {{"PlaceholderText", P::string("scrivi")}}), node("c4", "Frame", {})}),
              node("scroll", "ScrollingFrame", {{"Size", P::udim2(0, 100, 0, 100)}, {"Position", P::udim2(1, -110, 1, -110)}, {"CanvasSize", P::udim2(0, 100, 0, 400)}, {"CanvasPosition", P::vector2(0, 60)}},
                   {node("inner", "TextLabel", {{"Size", P::udim2(1, 0, 0, 50)}, {"Position", P::udim2(0, 0, 0, 80)}, {"ZIndex", P::number(3)}})})}),
        node("off", "ScreenGui", {{"Enabled", P::boolean(false)}}, {node("never", "Frame", {})}),
        node("front", "ScreenGui", {{"DisplayOrder", P::number(5)}}, {node("toast", "TextLabel", {{"Size", P::udim2(0, 300, 0, 40)}, {"Position", P::udim2(0.5, 0, 0, 0)}, {"AnchorPoint", P::vector2(0.5, 0)}, {"TextScaled", P::boolean(true)}})})};
    for (const auto& layout : g["layouts"].items()) {
        auto elements = gui_layout::resolve(screens, layout["size"][0].number(), layout["size"][1].number());
        Json actual = Json::array();
        for (const auto& e : elements) {
            Json clip = e.clip.null ? Json() : Json::array({e.clip.minX(), e.clip.minY(), e.clip.width(), e.clip.height()});
            actual.push(Json::object({{"id", e.id}, {"role", roleName(e.role)}, {"frame", Json::array({e.frame.minX(), e.frame.minY(), e.frame.width(), e.frame.height()})}, {"clip", clip},
                                      {"background", e.background}, {"backgroundAlpha", e.backgroundAlpha}, {"text", e.text}, {"hint", e.hint}, {"textSize", e.textSize}, {"font", e.font},
                                      {"scaled", e.scaled}, {"horizontal", e.horizontal}, {"vertical", e.vertical}}));
        }
        CHECK(golden::same(actual, layout["elements"], golden::Options{1e-9, true}) == "");
    }
}

TEST_CASE("Lim: Terrain.fillSphere sul server", "[scripting][lim][terrain]") {
    golden::registry();
    Harness h;
    Terrain ground;
    h.engine.terrain = &ground;
    auto output = h.run("let n = Terrain.fillSphere(center: Vec3(0, 8, 0), radius: 6, material: .grass)\nprint(n > 0)\nprint(Terrain.isSolid(Vec3(0, 8, 0)))");
    CHECK(output == std::vector<std::string>{"true", "true"});
    CHECK(ground.solid(0, 8, 0));
    Terrain snapshot = ground;
    h.lines.clear();
    h.engine.execute("Terrain.fillSphere(center: Vec3(0, 8, 0), radius: 3, material: .air)", ScriptContext::client, "StarterPlayer.Prova");
    for (int i = 0; i < 10 && h.engine.running() > 0; ++i) h.engine.step(1.0 / 30);
    REQUIRE_FALSE(h.lines.empty());
    CHECK(h.lines.front().error());
    CHECK(ground == snapshot);
    auto bad = h.run("Terrain.fillSphere(center: Vec3(0, 8, 0), radius: -1, material: .rock)");
    REQUIRE(bad.size() == 1);
    CHECK(h.lines.front().error());
    CHECK(ground == snapshot);
}

TEST_CASE("Lim: esempi in una sessione di prova", "[scripting][lim][creator][session]") {
    golden::registry();
    std::vector<std::string> lines;
    WorldBlock lava = base();
    lava.name = "Lava";
    std::vector<InstanceRecord> records = {
        record("Script", "Lava", "ServerScriptService",
               "let lava = World.find(\"Lava\")\n\nif lava != none {\n    on lava.touched(object) {\n        let character = object.character\n\n        if character != none {\n            character.health = 0\n            print(\"lava\", character.player?.name ?? \"\")\n        }\n    }\n}\n"),
        record("Script", "Ingresso", "ServerScriptService", "on Players.joined(player) {\n    print(\"\\(player.name) joined\")\n}\n"),
        record("Script", "Uscita", "ServerScriptService", "on Players.left(player) {\n    print(\"\\(player.name) left\")\n}\n"),
        record("Script", "Elenco", "ServerScriptService", "await Task.wait(0.05)\nfor player in Players.all {\n    print(\"presente\", player.name, player.id >= 0)\n}\n"),
        record("Script", "Rete", "ServerScriptService", "on Network.event(\"Jump\").received(player) {\n    print(\"salto da\", player.name)\n}\n"),
        record("Script", "Danno", "ServerScriptService",
               "fn damage(target: Character, amount: Number) -> Number {\n    target.health -= amount\n    return target.health\n}\nawait Task.wait(0.05)\nlet p = Players.all[0]\nlet c = p.character\nif c != none {\n    print(\"salute\", damage(c, 10), c.maxHealth, c is Character)\n}\n"),
        record("LocalScript", "Menu", "StarterPlayer.StarterPlayerScripts",
               "let button = TextButton()\nbutton.text = \"Play\"\n\nUI.player.add(button)\n\non button.clicked {\n    print(\"Clicked\")\n}\nNetwork.event(\"Jump\").send()\n"),
    };
    WorldSession session(experience(records, {lava}), WorldSession::Mode::play, "Tester", 7, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.physics = false;
    session.start({0, 4, 0});
    for (int frame = 0; frame < 4; ++frame) {
        session.place(0, 4, 0, 0);
        session.advance(1.0 / 30);
    }
    INFO(joined(lines));
    CHECK(contains(lines, "Tester joined"));
    CHECK(contains(lines, "presente Tester true"));
    CHECK(contains(lines, "salto da Tester"));
    CHECK(contains(lines, "salute 90 100 true"));
    auto shown = session.gui();
    auto screen = std::find_if(shown.begin(), shown.end(), [](const GuiNode& n) { return n.name == "UI"; });
    REQUIRE(screen != shown.end());
    REQUIRE(screen->children.size() == 1);
    CHECK(screen->children[0].className == "TextButton");
    CHECK(screen->children[0].text("Text") == "Play");
    session.activate(screen->children[0].id);
    session.advance(1.0 / 30);
    CHECK(contains(lines, "Clicked"));
    CHECK_FALSE(session.dead());
    for (int frame = 0; frame < 20 && !session.dead(); ++frame) {
        session.place(0, 1, 0, 0);
        session.advance(1.0 / 30);
    }
    CHECK(contains(lines, "lava Tester"));
    CHECK(session.dead());
    CHECK_FALSE(contains(lines, "Tester left"));
    session.stop();
    CHECK(std::count(lines.begin(), lines.end(), "Tester left") == 1);
    session.stop();
    CHECK(std::count(lines.begin(), lines.end(), "Tester left") == 1);
}

TEST_CASE("Lim: superficie pubblica implementata", "[scripting][lim][surface]") {
    golden::registry();
    const auto& registry = EngineTypes::shared().registry();
    std::set<std::string> exposed;
    for (const auto& [name, type] : registry.classes()) {
        auto names = EngineTypes::shared().members(name);
        for (const auto& [member, event] : type.events) {
            if (!names || !names->events.count(member) || names->events.at(member)[0] == '$') continue;
            INFO(name << "." << member);
            CHECK(EngineTypes::emittedEvents().count(names->events.at(member)) == 1);
            exposed.insert(names->events.at(member));
        }
    }
    CHECK(exposed == EngineTypes::emittedEvents());
    CHECK(registry.event("Part", "touched"));
    CHECK(registry.event("Players", "left"));
    CHECK_FALSE(registry.event("Player", "chatted"));
    CHECK_FALSE(registry.event("Humanoid", "climbing"));
    std::vector<std::string> lines;
    auto check = [&](const std::string& source) {
        lines.clear();
        WorldSession session(experience({record("Script", "Prova", "ServerScriptService", source)}, {base()}), WorldSession::Mode::play, "Tester", 7, false, [&](const OutputLine& l) { lines.push_back(l.text); });
        session.physics = false;
        session.start({0, 4, 0});
        session.advance(1.0 / 30);
        session.stop();
        return joined(lines);
    };
    CHECK(check("on Players.all[0].chatted(message) {\n    print(message)\n}\n").find("chatted") != std::string::npos);
    CHECK(check("on World.childAdded(child) {\n    print(\"aggiunto\", child.name)\n}\nlet part = Part(World, \"Nuovo\")\n").find("aggiunto Nuovo") != std::string::npos);
}

TEST_CASE("Lim: tipi graduali e sottoclassi", "[scripting][lim][types]") {
    golden::registry();
    Harness h;
    CHECK(h.run("let x = World.find(\"Baseplate\")\nif x != none {\n    print(x.anchored)\n}\nprint(\"fatto\")") == std::vector<std::string>{"fatto"});
    auto wrong = h.run("let f = Folder(parent: World, name: \"Cartella\")\nlet x = World.find(\"Cartella\")\nif x != none {\n    print(x.anchored)\n}");
    REQUIRE(wrong.size() == 1);
    CHECK(h.lines.front().error());
    CHECK(wrong.front().find("anchored") != std::string::npos);
    auto unknown = h.run("let x = World.find(\"Cartella\")\nif x != none {\n    print(x.nonEsiste)\n}");
    REQUIRE(unknown.size() == 1);
    CHECK(unknown.front().find("non è un membro di Instance") != std::string::npos);
    auto client = h.run("Network.event(\"X\").send(1)");
    REQUIRE(client.size() == 1);
    CHECK(client.front().find("disponibile solo negli script") != std::string::npos);
}

TEST_CASE("Lim: Storage.player", "[scripting][lim][storage]") {
    golden::registry();
    std::vector<std::string> lines;
    std::optional<Json> stored;
    int calls = 0;
    std::vector<InstanceRecord> records = {
        record("Script", "Profilo", "ServerScriptService",
               "async fn loadProfile(player: Player) {\n    let profile = await Storage.player(player.id).load()\n    print(\"primo\", profile ?? \"vuoto\")\n    print(\"salvato\", await Storage.player(player).save([\"monete\": 12, \"nome\": player.name]))\n    let again = await Storage.player(player.id).load()\n    print(\"secondo\", again)\n}\non Players.joined(player) {\n    await loadProfile(player)\n}\n"),
        record("Script", "Altro", "ServerScriptService", "await Task.wait(0.2)\nStorage.player(424242).load()\n"),
        record("LocalScript", "Client", "StarterPlayer.StarterPlayerScripts", "let me = Players.localPlayer\nif me != none {\n    Storage.player(me).load()\n}\n"),
    };
    WorldSession session(experience(records), WorldSession::Mode::play, "Ada", 31, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.engine->storage = [&](const std::string& action, const Json& value, ScriptEngine::StorageReply reply) {
        ++calls;
        if (action == "save") stored = value;
        reply(action == "save" ? std::nullopt : stored, "");
    };
    session.physics = false;
    session.start({0, 4, 0});
    for (int i = 0; i < 12; ++i) session.advance(1.0 / 30);
    INFO(joined(lines));
    CHECK(contains(lines, "primo vuoto"));
    CHECK(contains(lines, "salvato true"));
    REQUIRE(stored);
    CHECK((*stored)["monete"].number() == 12);
    CHECK((*stored)["nome"].string() == "Ada");
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("secondo ", 0) == 0 && l.find("12") != std::string::npos; }));
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("solo i dati del proprio giocatore") != std::string::npos; }));
    CHECK(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("disponibile solo negli script") != std::string::npos; }));
    CHECK(calls == 3);
    session.stop();

    std::vector<std::string> failing;
    WorldSession offline(experience({record("Script", "Profilo", "ServerScriptService", "on Players.joined(player) {\n    print(\"letto\", await Storage.player(player).load() ?? \"none\")\n    print(\"scritto\", await Storage.player(player).save(1))\n}\n")}),
                         WorldSession::Mode::play, "Bea", 5, false, [&](const OutputLine& l) { failing.push_back(l.text); });
    offline.engine->storage = [](const std::string&, const Json&, ScriptEngine::StorageReply reply) { reply(std::nullopt, "servizio non raggiungibile"); };
    offline.physics = false;
    offline.start({0, 4, 0});
    for (int i = 0; i < 6; ++i) offline.advance(1.0 / 30);
    INFO(joined(failing));
    CHECK(contains(failing, "letto none"));
    CHECK(contains(failing, "scritto false"));
    CHECK(std::any_of(failing.begin(), failing.end(), [](const std::string& l) { return l.find("servizio non raggiungibile") != std::string::npos; }));
    offline.stop();
}

TEST_CASE("Lim: versioni e deprecazioni", "[scripting][lim][versions]") {
    golden::registry();
    Harness h;
    auto lines = h.run("let r = RemoteEvent(parent: Storage, name: \"Vecchio\")\non r.onServerEvent(player) {\n    print(\"ricevuto\")\n}\nprint(\"pronto\")");
    REQUIRE(lines.size() >= 2);
    CHECK(lines[0].find("«onServerEvent» è deprecato: usa «received»") != std::string::npos);
    CHECK(h.lines[0].kind == OutputLine::Kind::warning);
    CHECK(contains(lines, "pronto"));
    auto clean = h.run("let r = RemoteEvent(parent: Storage, name: \"Nuovo\")\non r.received(player) {\n    print(\"ricevuto\")\n}\nprint(\"pronto\")");
    CHECK(clean == std::vector<std::string>{"pronto"});

    Experience world = Experience::blank();
    CHECK(world.languageVersion == std::string(rules::lim::languageVersion));
    CHECK(world.engineApiVersion == rules::lim::engineApiVersion);
    CHECK_FALSE(world.compatibility());
    auto json = world.json();
    CHECK(json["languageVersion"] == Json(std::string(rules::lim::languageVersion)));
    CHECK(json["engineApiVersion"] == Json(rules::lim::engineApiVersion));
    Json legacy = json;
    legacy.erase("languageVersion");
    legacy.erase("engineApiVersion");
    auto old = Experience::fromJson(legacy);
    REQUIRE(old);
    CHECK(old->languageVersion == std::string(rules::lim::languageVersion));
    CHECK(old->engineApiVersion == rules::lim::engineApiVersion);
    Experience future = world;
    future.engineApiVersion = rules::lim::engineApiVersion + 1;
    REQUIRE(future.compatibility());
    CHECK(future.compatibility()->find("Aggiorna Ludissey") != std::string::npos);
    future = world;
    future.languageVersion = "2.0";
    CHECK(future.compatibility());
    StudioDocument document;
    CHECK_FALSE(document.open(future));
    CHECK_FALSE(document.opened);
}

TEST_CASE("Lim: Teams e SpawnLocation", "[scripting][session][teams][spawn]") {
    golden::registry();
    std::vector<std::string> lines;
    auto team = [](const std::string& name, const std::string& color, bool automatic) {
        InstanceRecord r = record("Team", name, "Teams");
        r.properties["TeamColor"] = PropertyValue::color3(color);
        r.properties["AutoAssignable"] = PropertyValue::boolean(automatic);
        return r;
    };
    auto location = [](const std::string& name, double x, double z, std::optional<std::string> color, bool enabled) {
        WorldBlock block = base();
        block.id = makeUuid();
        block.kind = "spawn";
        block.name = name;
        block.x = x;
        block.z = z;
        block.y = 2;
        block.properties["Enabled"] = PropertyValue::boolean(enabled);
        block.properties["Neutral"] = PropertyValue::boolean(!color.has_value());
        if (color) block.properties["TeamColor"] = PropertyValue::color3(*color);
        return block;
    };
    std::vector<WorldBlock> blocks = {base(), location("Spenta", 30, 0, std::nullopt, false), location("Blu", -12, 4, std::string("0000FF"), true), location("Rossa", 10, -6, std::string("FF0000"), true)};
    std::vector<InstanceRecord> records = {team("Rossi", "FF0000", true), team("Spettatori", "888888", false),
                                           record("Script", "Squadra", "ServerScriptService", "on Players.joined(player) {\n    let team = player.team\n    if team != none {\n        print(team.name, player.neutral, Teams.find(\"Rossi\") == team)\n    }\n}\n")};
    WorldSession session(experience(records, blocks), WorldSession::Mode::play, "Tester", 7, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.physics = false;
    REQUIRE(session.teamColor());
    CHECK(*session.teamColor() == "FF0000");
    Terrain terrain;
    CharacterState arrival = motion::arrival(blocks, terrain, session.teamColor());
    CHECK(arrival.x == 10);
    CHECK(arrival.z == -6);
    CHECK(arrival.y == Catch::Approx(2.5));
    session.start(arrival.position());
    session.advance(1.0 / 30);
    INFO(joined(lines));
    CHECK(contains(lines, "Rossi false true"));
    session.stop();

    CharacterState neutral = motion::arrival(blocks, terrain, std::nullopt);
    CHECK(neutral.x == 0);
    CHECK(neutral.z == 5);
    blocks.push_back(location("Neutra", 4, 8, std::nullopt, true));
    CHECK(motion::arrival(blocks, terrain, std::nullopt).x == 4);
    CHECK(motion::arrival(blocks, terrain, std::string("00FF00")).z == 8);
    CHECK(motion::arrival(blocks, terrain, std::string("0000FF")).x == -12);

    WorldSession alone(experience({team("Spettatori", "888888", false)}), WorldSession::Mode::play, "Tester", 7, false, [](const OutputLine&) {});
    CHECK_FALSE(alone.teamColor());
}

TEST_CASE("Lim: Seat", "[scripting][session][seat]") {
    golden::registry();
    std::vector<std::string> lines;
    WorldBlock seat = base();
    seat.id = makeUuid();
    seat.kind = "seat";
    seat.name = "Sedia";
    seat.x = 6;
    seat.y = 0.5;
    std::vector<InstanceRecord> records = {record("Script", "Sedia", "ServerScriptService",
                                                  "let seat = World.find(\"Sedia\")\nif seat != none {\n    on seat.changed(property) {\n        print(property, seat.occupant != none)\n    }\n}\n")};
    WorldSession session(experience(records, {base(), seat}), WorldSession::Mode::play, "Tester", 7, false, [&](const OutputLine& l) { lines.push_back(l.text); });
    session.physics = false;
    session.start({0, 1, 0});
    double normal = session.speedFactor();
    CHECK(normal > 0);
    session.place(0, 1, 0, 0);
    session.advance(1.0 / 30);
    CHECK_FALSE(session.sitting());
    CHECK_FALSE(session.teleport());
    session.place(6.3, 1, 0.2, 0.5);
    session.advance(1.0 / 30);
    INFO(joined(lines));
    REQUIRE(session.sitting());
    CHECK(session.speedFactor() == 0);
    CHECK(session.jumpFactor() > 0);
    auto moved = session.teleport();
    REQUIRE(moved);
    CHECK(moved->x == Catch::Approx(6));
    CHECK(moved->y == Catch::Approx(1));
    CHECK(moved->z == Catch::Approx(0));
    CHECK(contains(lines, "Occupant true"));
    for (int frame = 0; frame < 10; ++frame) {
        session.place(6, 1, 0, 0.5);
        session.advance(1.0 / 30);
        CHECK(session.sitting());
        CHECK_FALSE(session.teleport());
    }
    session.place(6, 1.8, 0, 0.5);
    session.advance(1.0 / 30);
    CHECK_FALSE(session.sitting());
    CHECK(session.speedFactor() == normal);
    CHECK(contains(lines, "Occupant false"));
    session.place(6, 1, 0, 0.5);
    session.advance(1.0 / 30);
    CHECK_FALSE(session.sitting());
    for (int frame = 0; frame < 70; ++frame) {
        session.place(0, 1, 0, 0);
        session.advance(1.0 / 30);
    }
    session.place(6, 1, 0, 0);
    session.advance(1.0 / 30);
    CHECK(session.sitting());
    session.kill();
    session.advance(1.0 / 30);
    CHECK_FALSE(session.sitting());
    session.stop();
}

namespace {

class RecordingOutput final : public audio::Output {
public:
    std::vector<std::string> log;
    std::map<std::string, audio::Playback> active;
    bool play(const std::string& id, const audio::Playback& playback) override {
        active[id] = playback;
        log.push_back("play " + playback.file.filename().string() + " " + formatNumber(playback.volume) + " " + formatNumber(playback.speed) + (playback.looped ? " loop" : ""));
        return true;
    }
    void update(const std::string& id, const audio::Playback& playback) override {
        active[id] = playback;
        log.push_back("update " + formatNumber(playback.volume) + " " + formatNumber(playback.speed));
    }
    bool playing(const std::string& id) override { return active.count(id) > 0; }
    void stop(const std::string& id) override {
        if (active.erase(id)) log.push_back("stop");
    }
};

}

TEST_CASE("Lim: Sound", "[scripting][session][audio]") {
    golden::registry();
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Audio " + makeUuid());
    asset_cache::setFolder(folder);
    std::string data = "RIFF audio di prova";
    std::string asset = asset_cache::identifier(data, "wav");
    asset_cache::store(data, asset);
    auto output = std::make_shared<RecordingOutput>();
    audio::setOutput(output);
    auto sound = [&](const std::string& name, const std::string& source, bool playing) {
        InstanceRecord r = record("Sound", name, "SoundService");
        r.properties["SoundId"] = PropertyValue::content(source);
        r.properties["Volume"] = PropertyValue::number(0.8);
        r.properties["Looped"] = PropertyValue::boolean(playing);
        r.properties["Playing"] = PropertyValue::boolean(playing);
        return r;
    };
    std::vector<std::string> lines;
    std::vector<InstanceRecord> records = {
        sound("Musica", asset_cache::reference(asset), true), sound("Effetto", asset_cache::reference(asset), false), sound("Rotto", "ludissey://asset/" + std::string(64, 'a') + ".wav", false),
        record("Script", "Regia", "ServerScriptService",
               "let effect = Audio.find(\"Effetto\")\nlet broken = Audio.find(\"Rotto\")\nif effect != none && broken != none {\n    effect.play()\n    await Task.wait(0.05)\n    effect.volume = 0.25\n    effect.playbackSpeed = 1.5\n    await Task.wait(0.05)\n    effect.stop()\n    print(\"fermo\", effect.playing)\n    broken.play()\n    print(\"rotto\", broken.playing)\n}\n")};
    {
        WorldSession session(experience(records), WorldSession::Mode::play, "Tester", 7, false, [&](const OutputLine& l) { lines.push_back(l.text); });
        session.physics = false;
        session.start({0, 1, 0});
        for (int frame = 0; frame < 8; ++frame) session.advance(1.0 / 30);
        INFO(joined(lines));
        INFO(joined(output->log));
        std::string file = asset;
        CHECK(contains(output->log, "play " + file + " 0.8 1 loop"));
        CHECK(contains(output->log, "play " + file + " 0.8 1"));
        CHECK(contains(output->log, "update 0.25 1"));
        CHECK(contains(output->log, "update 0.25 1.5"));
        CHECK(contains(lines, "fermo false"));
        CHECK(contains(lines, "rotto false"));
        CHECK(contains(lines, "Sound «Rotto»: risorsa audio mancante, importala dalla proprietà SoundId"));
        CHECK(output->active.size() == 1);
        session.stop();
        CHECK(output->active.empty());
    }
    Experience world = experience(records);
    auto referenced = AssetService::references(world);
    CHECK(referenced.size() == 2);
    CHECK(std::count(referenced.begin(), referenced.end(), AssetId(asset)) == 1);
    CHECK(AssetService::meshes(world).empty());
    audio::setOutput(nullptr);
    asset_cache::setFolder(std::nullopt);
    std::filesystem::remove_all(folder);
}
