#include "StudioView.hpp"

#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Scripting/EngineTypes.hpp"
#include "Engine/Studio/MacArchive.hpp"
#include "Engine/Studio/StudioSync.hpp"
#include "Lim/Tooling/Reference.hpp"
#include "Lim/Tooling/Tooling.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace studio {

using namespace ui;
using namespace ui::theme;
using ld::tr;
using ld::trn;

namespace {

const std::vector<std::string> skyColors{"5B8ED6", "3E6FB8", "8FB8E8", "CFE3F7", "F2C38B", "E58A6B", "2B2F4A", "FFFFFF"};
const std::vector<std::string> ambientColors{"8A8FA0", "5B6169", "B4B9C6", "7A6A8F", "9AA0A8", "3B3F4A"};
const std::vector<std::string> toolboxCategories{"parts", "models"};
const std::vector<ld::TerrainTool> terrainTools{ld::TerrainTool::add, ld::TerrainTool::subtract, ld::TerrainTool::raise, ld::TerrainTool::paint, ld::TerrainTool::smooth, ld::TerrainTool::flatten, ld::TerrainTool::water};
const std::vector<ld::TerrainMaterial> terrainSolids{ld::TerrainMaterial::grass, ld::TerrainMaterial::rock, ld::TerrainMaterial::sand, ld::TerrainMaterial::mud};
const Font console = type::code;

Color markOn(Color fill) { return 0.299 * fill.r + 0.587 * fill.g + 0.114 * fill.b > 0.62 ? colors::textPrimary : colors::textOnAccent; }

struct PanelInfo {
    std::string key, title, symbol;
};

const std::vector<std::string> utilities{"output", "performance", "api"};
const std::vector<std::string> layoutPanels{"explorer", "properties", "toolbox", "output", "performance", "api"};
constexpr const char* dockKey = "LudisseyStudioDockWidth";
constexpr const char* shareKey = "LudisseyStudioExplorerShare";
constexpr const char* propertiesKey = "LudisseyStudioPropertiesWidth";
constexpr const char* utilityKey = "LudisseyStudioUtilityHeight";
constexpr const char* panelsKey = "LudisseyStudioPanels";
constexpr const char* tabKey = "LudisseyStudioUtilityTab";
constexpr const char* collapsedKey = "LudisseyStudioUtilityCollapsed";

const std::vector<PanelInfo> panelList{{"explorer", "Esplora", "ld-icon-panel-explorer"}, {"properties", "Proprietà", "ld-icon-panel-properties"}, {"toolbox", "Casella strumenti", "ld-icon-panel-toolbox"}, {"output", "Output", "ld-icon-panel-output"}, {"performance", "Prestazioni script", "ld-icon-studio-run"}, {"api", "API Lim", "ld-icon-service-scripts"}};

std::string panelTitle(const std::string& key) {
    if (key == "explorer") return tr("Esplora");
    if (key == "properties") return tr("Proprietà");
    if (key == "toolbox") return tr("Casella strumenti");
    if (key == "terrain") return tr("Terreno");
    if (key == "output") return tr("Output");
    if (key == "performance") return tr("Prestazioni script");
    return tr("API Lim");
}

std::string vectorSeparator() { return ld::localization::language() == ld::Language::italian ? "; " : ", "; }

std::vector<std::string> vectorParts(const std::string& text) {
    char divider = text.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t next = text.find(divider, start);
        std::string piece = text.substr(start, next == std::string::npos ? std::string::npos : next - start);
        if (!ld::trim(piece).empty()) parts.push_back(piece);
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return parts;
}

std::optional<double> parseNumber(std::string text) {
    for (auto& c : text)
        if (c == ',') c = '.';
    auto trimmed = ld::trim(text);
    if (trimmed.empty()) return std::nullopt;
    char* end = nullptr;
    double value = std::strtod(trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::string components(const std::string& hex) {
    ld::Color c = ld::Color::hex(hex, {0, 0, 0, 1});
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "#%02X%02X%02X", static_cast<int>(std::lround(c.r * 255)), static_cast<int>(std::lround(c.g * 255)), static_cast<int>(std::lround(c.b * 255)));
    return buffer;
}

std::optional<std::string> parseComponents(const std::string& text) {
    std::string trimmed = ld::trim(text);
    std::string digits = !trimmed.empty() && trimmed[0] == '#' ? trimmed.substr(1) : trimmed;
    if (digits.size() == 6 && std::all_of(digits.begin(), digits.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) return ld::uppercase(digits);
    std::vector<int> parts;
    for (const auto& piece : vectorParts(trimmed)) {
        std::string value = ld::trim(piece);
        char* end = nullptr;
        long number = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || end != value.c_str() + value.size()) return std::nullopt;
        parts.push_back(static_cast<int>(number));
    }
    if (parts.size() != 3) return std::nullopt;
    for (int p : parts)
        if (p < 0 || p > 255) return std::nullopt;
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%02X%02X%02X", parts[0], parts[1], parts[2]);
    return std::string(buffer);
}

}

}

namespace studio {

namespace {

std::string localStamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

std::string propertyLabel(std::string_view name) {
    if (name == "Name") return tr("Nome");
    if (name == "ClassName") return tr("Classe");
    if (name == "Parent") return tr("Contenitore");
    if (name == "Anchored") return tr("Ancorato");
    if (name == "CanCollide") return tr("Collisioni");
    if (name == "Color") return tr("Colore");
    if (name == "Material") return tr("Materiale");
    if (name == "Transparency") return tr("Trasparenza");
    if (name == "Reflectance") return tr("Riflettenza");
    if (name == "Locked") return tr("Bloccato");
    if (name == "Position") return tr("Posizione");
    if (name == "Size") return tr("Dimensioni");
    if (name == "Orientation") return tr("Orientamento");
    if (name == "Velocity") return tr("Velocità");
    if (name == "Shape") return tr("Forma");
    if (name == "MeshId") return tr("Modello 3D");
    if (name == "AnchorPoint") return tr("Punto di ancoraggio");
    if (name == "AutoAssignable") return tr("Assegnazione automatica");
    if (name == "AutoButtonColor") return tr("Colore automatico al clic");
    if (name == "BackgroundColor3") return tr("Colore di sfondo");
    if (name == "BackgroundTransparency") return tr("Trasparenza dello sfondo");
    if (name == "BorderColor3") return tr("Colore del bordo");
    if (name == "BorderSizePixel") return tr("Spessore del bordo");
    if (name == "CanBeDropped") return tr("Si può lasciare");
    if (name == "CanvasPosition") return tr("Posizione del contenuto");
    if (name == "CanvasSize") return tr("Dimensioni del contenuto");
    if (name == "CellPadding") return tr("Spazio fra le celle");
    if (name == "CellSize") return tr("Dimensioni delle celle");
    if (name == "Character") return tr("Personaggio");
    if (name == "ClearTextOnFocus") return tr("Svuota al clic");
    if (name == "ClipsDescendants") return tr("Ritaglia il contenuto");
    if (name == "Disabled") return tr("Disattivato");
    if (name == "DisplayOrder") return tr("Ordine di visualizzazione");
    if (name == "Enabled") return tr("Attivo");
    if (name == "FillDirection") return tr("Direzione");
    if (name == "Font") return tr("Carattere");
    if (name == "Health") return tr("Salute");
    if (name == "HorizontalAlignment") return tr("Allineamento orizzontale");
    if (name == "Image") return tr("Immagine");
    if (name == "ImageColor3") return tr("Colore dell’immagine");
    if (name == "ImageTransparency") return tr("Trasparenza dell’immagine");
    if (name == "JumpPower") return tr("Forza del salto");
    if (name == "LayoutOrder") return tr("Ordine nel layout");
    if (name == "Looped") return tr("Ripetizione continua");
    if (name == "MaxHealth") return tr("Salute massima");
    if (name == "Neutral") return tr("Neutrale");
    if (name == "Occupant") return tr("Occupante");
    if (name == "Padding") return tr("Spaziatura");
    if (name == "PaddingBottom") return tr("Margine inferiore");
    if (name == "PaddingLeft") return tr("Margine sinistro");
    if (name == "PaddingRight") return tr("Margine destro");
    if (name == "PaddingTop") return tr("Margine superiore");
    if (name == "PlaceholderText") return tr("Testo segnaposto");
    if (name == "PlaybackSpeed") return tr("Velocità di riproduzione");
    if (name == "Playing") return tr("In riproduzione");
    if (name == "PrimaryPart") return tr("Parte principale");
    if (name == "RequiresHandle") return tr("Richiede l’impugnatura");
    if (name == "ResetOnSpawn") return tr("Ripristina alla rinascita");
    if (name == "ScrollBarThickness") return tr("Spessore della barra");
    if (name == "ScrollingEnabled") return tr("Scorrimento attivo");
    if (name == "SortOrder") return tr("Ordinamento");
    if (name == "SoundId") return tr("Suono");
    if (name == "Source") return tr("Sorgente");
    if (name == "TeamColor") return tr("Colore della squadra");
    if (name == "Text") return tr("Testo");
    if (name == "TextColor3") return tr("Colore del testo");
    if (name == "TextScaled") return tr("Testo adattato");
    if (name == "TextSize") return tr("Dimensione del testo");
    if (name == "TextTransparency") return tr("Trasparenza del testo");
    if (name == "TextWrapped") return tr("A capo automatico");
    if (name == "TextXAlignment") return tr("Allineamento orizzontale del testo");
    if (name == "TextYAlignment") return tr("Allineamento verticale del testo");
    if (name == "ToolTip") return tr("Suggerimento");
    if (name == "UserId") return tr("ID utente");
    if (name == "Value") return tr("Valore");
    if (name == "VerticalAlignment") return tr("Allineamento verticale");
    if (name == "Visible") return tr("Visibile");
    if (name == "Volume") return tr("Volume");
    if (name == "WalkSpeed") return tr("Velocità di camminata");
    if (name == "ZIndex") return tr("Livello di sovrapposizione");
    if (name == "ClockTime") return tr("Ora del giorno");
    if (name == "Brightness") return tr("Luminosità");
    if (name == "FogEnd") return tr("Distanza della nebbia");
    if (name == "Ambient") return tr("Luce ambientale");
    if (name == "SkyTop") return tr("Cielo in alto");
    if (name == "SkyHorizon") return tr("Cielo all’orizzonte");
    return std::string(name);
}

std::string classLabel(std::string_view name) {
    if (name == "Backpack") return tr("Zaino");
    if (name == "BindableEvent") return tr("Evento locale");
    if (name == "BindableFunction") return tr("Funzione locale");
    if (name == "BoolValue") return tr("Valore sì/no");
    if (name == "Color3Value") return tr("Valore colore");
    if (name == "Folder") return tr("Cartella");
    if (name == "Frame") return tr("Riquadro");
    if (name == "Humanoid") return tr("Umanoide");
    if (name == "ImageButton") return tr("Pulsante con immagine");
    if (name == "ImageLabel") return tr("Immagine");
    if (name == "IntValue") return tr("Valore intero");
    if (name == "LocalScript") return tr("Script del giocatore");
    if (name == "MeshPart") return tr("Parte con modello 3D");
    if (name == "Model") return tr("Modello");
    if (name == "ModuleScript") return tr("Modulo");
    if (name == "NumberValue") return tr("Valore numerico");
    if (name == "ObjectValue") return tr("Riferimento a un oggetto");
    if (name == "Part") return tr("Parte");
    if (name == "Player") return tr("Giocatore");
    if (name == "PlayerGui") return tr("Interfaccia del giocatore");
    if (name == "PlayerScripts") return tr("Cartella degli script del giocatore");
    if (name == "RemoteEvent") return tr("Evento di rete");
    if (name == "RemoteFunction") return tr("Funzione di rete");
    if (name == "ScreenGui") return tr("Interfaccia a schermo");
    if (name == "Script") return tr("Script del server");
    if (name == "ScrollingFrame") return tr("Riquadro scorrevole");
    if (name == "Seat") return tr("Sedile");
    if (name == "Sound") return tr("Suono");
    if (name == "SpawnLocation") return tr("Punto di partenza");
    if (name == "StarterCharacterScripts") return tr("Script iniziali del personaggio");
    if (name == "StarterPlayerScripts") return tr("Script iniziali del giocatore");
    if (name == "StringValue") return tr("Valore testo");
    if (name == "Team") return tr("Squadra");
    if (name == "TextBox") return tr("Campo di testo");
    if (name == "TextButton") return tr("Pulsante di testo");
    if (name == "TextLabel") return tr("Etichetta di testo");
    if (name == "Tool") return tr("Strumento");
    if (name == "UIGridLayout") return tr("Layout a griglia");
    if (name == "UIListLayout") return tr("Layout a elenco");
    if (name == "UIPadding") return tr("Margini");
    if (name == "Vector3Value") return tr("Valore vettore");
    if (name == "WedgePart") return tr("Cuneo");
    return std::string(name);
}

std::string categoryLabel(std::string_view name) {
    if (name == "Interaction") return tr("Interazione");
    if (name == "Effects") return tr("Effetti");
    if (name == "GUI") return tr("Interfaccia");
    if (name == "Values") return tr("Valori");
    if (name == "Avatar") return tr("Avatar");
    if (name == "Parts") return tr("Parti");
    if (name == "Scripts") return tr("Script");
    return tr("Altro");
}

std::string classText(const std::string& className) {
    return className;
}

std::string scriptName(std::string_view className, std::string_view property) { return tr("Negli script: {0}", ld::EngineTypes::shared().limProperty(className, property)); }

std::vector<HighlightSpan> highlightLim(const std::string& source) {
    static const auto& globals = ld::EngineTypes::shared().registry().globals();
    std::vector<HighlightSpan> spans;
    for (const auto& span : lim::tooling::highlight(source)) {
        HighlightSpan out;
        out.start = span.offset;
        out.length = span.length;
        switch (span.style) {
        case lim::tooling::TokenStyle::keyword: out.color = code::keyword; out.bold = true; break;
        case lim::tooling::TokenStyle::type: out.color = code::type; break;
        case lim::tooling::TokenStyle::number: out.color = code::number; break;
        case lim::tooling::TokenStyle::text: out.color = code::string; break;
        case lim::tooling::TokenStyle::comment: out.color = code::comment; break;
        case lim::tooling::TokenStyle::identifier:
            if (!globals.count(source.substr(span.offset, span.length))) continue;
            out.color = code::type;
            break;
        default: continue;
        }
        spans.push_back(out);
    }
    return spans;
}

ld::Async<void> saveProject(std::shared_ptr<ld::Store> store, std::shared_ptr<bool> alive, ld::StudioDocument* document, std::weak_ptr<StudioStage> weakStage, ld::Experience snapshot, ld::Experience outgoing, bool publish) {
    std::exception_ptr failure;
    bool unverified = false;
    ld::StudioReadBack result;
    try {
        result = co_await ld::StudioSync(store->backend).save(outgoing, publish);
    } catch (const ld::SaveVerificationError&) {
        failure = std::current_exception();
        unverified = true;
    } catch (...) {
        failure = std::current_exception();
    }
    if (failure) {
        std::string reason = ld::describe(failure);
        if (*alive) document->log(reason, true);
        throw ld::ServiceError(unverified ? tr("Salvataggio non riuscito. Riprova.") : reason + " " + tr("Il progetto non è stato modificato."));
    }
    if (*alive && document->opened && document->world.id == snapshot.id) document->adopt(result.draft, snapshot);
    co_await store->refreshWorlds();
    co_await store->loadDetails(snapshot.id);
    if (auto stage = weakStage.lock(); *alive && stage) {
        int width = std::max(1, static_cast<int>(stage->size.width * 2)), height = std::max(1, static_cast<int>(stage->size.height * 2));
        auto image = stage->snapshot(width, height);
        if (auto png = ld::encodePng(image.rgba, image.width, image.height)) {
            std::error_code error;
            std::filesystem::create_directories(ld::studio_assets::previews(), error);
            std::ofstream file(ld::studio_assets::preview(snapshot.id), std::ios::binary);
            file << *png;
        }
    }
    store->notice = publish ? tr("Pubblicata") : tr("Salvato");
    if (*alive) document->log(publish ? tr("Salvato e pubblicato") : tr("Salvato"), false);
}

ld::Async<void> openProject(std::shared_ptr<ld::Store> store, ld::StudioDocument* document, ld::Experience project) {
    auto opened = document->open(project);
    if (!opened) throw ld::ServiceError(opened.error().message);
    co_await store->loadDetails(project.id);
}

ld::Async<void> refreshAll(std::shared_ptr<ld::Store> store) { co_await store->refresh(); }

ld::Async<void> uploadFromMac(std::shared_ptr<ld::Store> store, ld::Experience world) {
    if (!store->profile) co_return;
    co_await ld::mac_archive::upload(store->backend, store->profile->username, world);
    co_await store->refreshWorlds();
}

std::string templateTitle(int kind) { return kind == 0 ? tr("Base") : kind == 1 ? tr("Terreno piatto") : kind == 2 ? tr("Isola") : tr("Percorso a ostacoli"); }

ld::Experience templateWorld(int kind) {
    ld::Experience world = ld::Experience::blank();
    if (kind == 0) return world;
    world.title = templateTitle(kind);
    if (kind == 1) {
        world.blocks.clear();
        world.terrain.fillRegion({-ld::rules::baseplate::width / 2.0, -ld::rules::terrain::cellSize * 2, -ld::rules::baseplate::width / 2.0}, {ld::rules::baseplate::width / 2.0, 0, ld::rules::baseplate::width / 2.0}, ld::TerrainMaterial::grass);
    } else if (kind == 2) {
        world.blocks.clear();
        ld::TerrainSettings settings;
        settings.seed = 7;
        world.terrain.generate(settings);
    } else {
        for (int i = 0; i < 6; ++i) {
            ld::WorldBlock step;
            step.id = ld::makeUuid();
            step.name = tr("Piattaforma") + std::to_string(i + 1);
            step.sizeX = 4;
            step.sizeY = 1;
            step.sizeZ = 4;
            step.x = -15 + i * 6;
            step.y = 1.5 + i * 1.5;
            step.z = i % 2 ? 3 : -3;
            step.color = std::string(ld::rules::part::palette[static_cast<std::size_t>(i * 3) % ld::rules::part::palette.size()]);
            world.blocks.push_back(step);
        }
    }
    return world;
}

ui::Image templateImage(int kind) {
    static std::map<int, ui::Image> cache;
    auto found = cache.find(kind);
    if (found != cache.end()) return found->second;
    ld::Experience world = templateWorld(kind);
    auto scene = ld::render::worldScene(world);
    auto camera = ld::render::Node::make("template.camera");
    camera->camera = ld::render::Camera{};
    camera->position = {34, 26, 34};
    camera->look({0, 0, 0});
    scene->scene.root->add(camera);
    auto rendered = ld::render::Renderer::shared().snapshot(scene->scene, *camera, 344, 194, 4, true);
    ui::Image picture = rendered.rgba.empty() ? ui::Image{} : ui::imageFromRgba(rendered.rgba, rendered.width, rendered.height);
    cache[kind] = picture;
    return picture;
}

}

StudioShell::StudioShell(std::shared_ptr<ld::Store> value) : store(std::move(value)) {
    theme::applyScale(theme::Scale::studio);
    loadLayout();
    document.changed = [this] {
        if (stage && stage->interacting()) {
            stage->refresh();
            deferredRefresh = true;
            return;
        }
        refresh();
    };
    overlay.invalidated = [this] { invalidate(); };
}

StudioShell::~StudioShell() {
    *alive = false;
    document.changed = nullptr;
    invalidated = nullptr;
    if (stage) { stage->invalidated = nullptr; stage->stop(); }
}

void StudioShell::invalidate() {
    if (invalidated) invalidated();
}

void StudioShell::refresh() {
    if (stage) stage->refresh();
    invalidate();
}

void StudioShell::openRequested() {
    auto& launch = ld::LaunchRequest::shared();
    if (!launch.studioProject) return;
    if (store->busy || store->restoringSession) return;
    std::string id = *launch.studioProject;
    launch.studioProject.reset();
    if (document.opened && document.world.id == id) return;
    for (const auto& world : store->worlds())
        if (world.id == id) {
            open(world);
            return;
        }
    store->error = tr("Progetto non disponibile.");
}

void StudioShell::open(const ld::Experience& project) {
    if (store->busy) return;
    if (project.owner.empty()) {
        auto opened = document.open(project);
        if (!opened) store->error = opened.error().message;
        invalidate();
        return;
    }
    store->perform([owner = store, document = &document, project] { return openProject(owner, document, project); });
}

void StudioShell::confirmLeaving(std::function<void()> proceed) {
    if (!document.opened || !document.dirty()) {
        proceed();
        invalidate();
        return;
    }
    leaving = std::move(proceed);
    invalidate();
}

void StudioShell::discardRecovery() {
    if (!document.world.id.empty()) ld::studio_recovery::discard(document.world.id);
    recoveriesLoaded = false;
}

void StudioShell::requestClose() {
    confirmLeaving([this] {
        discardRecovery();
        document.close();
    });
}

bool StudioShell::requestQuit() {
    if (!document.opened || !document.dirty()) return true;
    confirmLeaving([this] {
        discardRecovery();
        document.close();
        if (quit) quit();
    });
    return false;
}

void StudioShell::newProject() {
    if (store->busy || document.playing) return;
    confirmLeaving([this] {
        discardRecovery();
        document.close();
        open(ld::Experience::blank());
    });
}

void StudioShell::showProjects() {
    if (store->busy || document.playing) return;
    confirmLeaving([this] {
        discardRecovery();
        document.close();
    });
}

void StudioShell::saveAs() {
    if (!document.opened || store->busy || document.playing) return;
    if (static_cast<int>(store->worlds().size()) >= ld::rules::world::experiencesPerDeveloper) {
        store->error = tr("Hai già {0} progetti, il massimo. Eliminane uno per crearne un altro.", ld::rules::world::experiencesPerDeveloper);
        invalidate();
        return;
    }
    auto copy = document.world.copy(tr("Copia di {0}", document.world.title));
    auto previous = document.world.id;
    auto opened = document.open(copy);
    if (!opened) {
        store->error = opened.error().message;
        invalidate();
        return;
    }
    document.log(tr("Copia creata. Salvala per tenerla."), false);
    if (document.world.id != previous) save(false);
    invalidate();
}

void StudioShell::openSettings() {
    if (!document.opened) return;
    document.settingsOpen = true;
    invalidate();
}

void StudioShell::togglePanel(const std::string& name) {
    if (document.panels.count(name)) document.panels.erase(name);
    else {
        document.panels.insert(name);
        if (std::find(utilities.begin(), utilities.end(), name) != utilities.end()) {
            document.utilityTab = name;
            document.utilityCollapsed = false;
        }
    }
    invalidate();
}

void StudioShell::resetLayout() {
    ld::StudioDocument fresh;
    document.panels = fresh.panels;
    document.dockWidth = fresh.dockWidth;
    document.explorerShare = fresh.explorerShare;
    document.propertiesWidth = fresh.propertiesWidth;
    document.utilityHeight = fresh.utilityHeight;
    document.utilityTab = fresh.utilityTab;
    document.utilityCollapsed = fresh.utilityCollapsed;
    invalidate();
}

void StudioShell::loadLayout() {
    using ld::local_preferences::number;
    if (auto v = number(dockKey)) document.dockWidth = *v;
    if (auto v = number(shareKey)) document.explorerShare = *v;
    if (auto v = number(propertiesKey)) document.propertiesWidth = *v;
    if (auto v = number(utilityKey)) document.utilityHeight = *v;
    if (auto v = number(collapsedKey)) document.utilityCollapsed = *v != 0;
    if (auto v = number(tabKey); v && *v >= 0 && *v < static_cast<double>(utilities.size())) document.utilityTab = utilities[static_cast<std::size_t>(*v)];
    if (auto v = number(panelsKey)) {
        auto bits = static_cast<long long>(*v);
        document.panels.clear();
        for (std::size_t i = 0; i < layoutPanels.size(); ++i)
            if (bits & (1LL << i)) document.panels.insert(layoutPanels[i]);
    }
    savedLayout = layoutSignature();
}

std::string StudioShell::layoutSignature() const {
    long long bits = 0;
    for (std::size_t i = 0; i < layoutPanels.size(); ++i)
        if (document.panels.count(layoutPanels[i])) bits |= 1LL << i;
    auto tab = std::find(utilities.begin(), utilities.end(), document.utilityTab);
    return ld::localization::decimal(document.dockWidth, 0) + "|" + ld::localization::decimal(document.explorerShare * 1000, 0) + "|" + ld::localization::decimal(document.propertiesWidth, 0) + "|" + ld::localization::decimal(document.utilityHeight, 0) + "|" + std::to_string(bits) + "|" + std::to_string(tab - utilities.begin()) + "|" + (document.utilityCollapsed ? "1" : "0");
}

void StudioShell::saveLayout(double now) {
    if (now - layoutCheckedAt < 1) return;
    layoutCheckedAt = now;
    std::string current = layoutSignature();
    if (current == savedLayout) return;
    savedLayout = current;
    long long bits = 0;
    for (std::size_t i = 0; i < layoutPanels.size(); ++i)
        if (document.panels.count(layoutPanels[i])) bits |= 1LL << i;
    auto tab = std::find(utilities.begin(), utilities.end(), document.utilityTab);
    using ld::local_preferences::setNumber;
    setNumber(dockKey, document.dockWidth);
    setNumber(shareKey, document.explorerShare);
    setNumber(propertiesKey, document.propertiesWidth);
    setNumber(utilityKey, document.utilityHeight);
    setNumber(panelsKey, static_cast<double>(bits));
    setNumber(tabKey, static_cast<double>(tab == utilities.end() ? 0 : tab - utilities.begin()));
    setNumber(collapsedKey, document.utilityCollapsed ? 1 : 0);
}

void StudioShell::frameSelection() {
    if (stage && document.selection()) stage->frameSelection();
    invalidate();
}

void StudioShell::toggleView(const std::string& name) {
    if (name == "grid") document.showGrid = !document.showGrid;
    else if (name == "gui") document.showGui = !document.showGui;
    refresh();
}

void StudioShell::showHelp() {
    helpOpen = true;
    invalidate();
}

void StudioShell::recover(const ld::studio_recovery::Entry& entry) {
    if (store->busy) return;
    auto recovered = ld::studio_recovery::load(entry.id);
    if (!recovered) {
        ld::studio_recovery::discard(entry.id);
        recoveriesLoaded = false;
        store->error = recovered.error().message;
        invalidate();
        return;
    }
    ld::Experience base;
    bool known = false;
    for (const auto& world : store->worlds())
        if (world.id == entry.id) {
            base = world;
            known = true;
        }
    if (!known) {
        base = ld::Experience::blank();
        base.id = recovered->id;
        base.blocks = {ld::Experience::baseplate(base.id)};
    }
    auto opened = document.open(base);
    auto restored = opened ? document.recover(*recovered) : opened;
    if (!restored) store->error = restored.error().message;
    else recoveredAt = 0;
    recoveriesLoaded = false;
    invalidate();
}

void StudioShell::save(bool publish) {
    if (!document.opened || store->busy || document.playing) return;
    publishing = publish;
    auto snapshot = document.world;
    auto outgoing = document.publishable();
    if (!outgoing.ok()) {
        store->error = outgoing.error().message;
        document.log(outgoing.error().message, true);
        invalidate();
        return;
    }
    store->perform([owner = store, alive = alive, document = &document, stage = std::weak_ptr<StudioStage>(stage), snapshot, value = *outgoing, publish] { return saveProject(owner, alive, document, stage, snapshot, value, publish); });
}

void StudioShell::importModel() {
    if (!chooseMesh) return;
    auto file = chooseMesh();
    if (!file) return;
    auto result = document.importMesh(*file);
    if (!result.ok()) store->error = result.error().message;
    invalidate();
}

NodePtr StudioShell::headerText(NodePtr label) { return label->foreground(colors::textOnAccent)->shadow(emboss::dark.color, 0, -emboss::dark.dy, 0); }

NodePtr StudioShell::panelClose(const std::string& panel) {
    std::string id = "panel.close." + panel;
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered(id);
    return plainButton(id, glyph("chiudi", iconSize::compact - spacing::s2, colors::textOnAccent)->frameSize(iconSize::compact, iconSize::compact)->background(rectangle(hovered ? Color::white(shadows::utilityHover) : Color::clear()))->clipped(metrics::smallRadius), [this, panel] {
               togglePanel(panel);
               refresh();
           })->help(tr("Chiudi {0}", panelTitle(panel)));
}

NodePtr StudioShell::panelHeader(const std::string& title, const std::optional<std::string>& detail, const std::string& panel) {
    std::vector<NodePtr> items{headerText(text(title, type::smallStrong)), spacer(0.0)};
    if (detail) items.push_back(headerText(lineLimited(text(*detail, type::caption), 1)));
    if (!panel.empty()) items.push_back(panelClose(panel));
    return hstack(std::move(items), spacing::s4)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::panelHeader)->fillWidth()->background(sheet(sheen::panelHeader));
}

NodePtr StudioShell::build() {
    openRequested();
    if (setTitle) setTitle(document.opened ? "Ludissey Studio: " + document.world.title : "Ludissey Studio");
    if (setEdited) setEdited(document.opened && document.dirty());
    if (document.opened && !stage) {
        stage = std::make_shared<StudioStage>(document);
        stage->invalidated = [this] { invalidate(); };
    }
    if (!document.opened && stage) {
        stage->stop();
        stage.reset();
    }
    if (stage) {
        auto look = store->currentAvatar();
        stage->avatar = look ? *look : ld::AvatarConfiguration::standard("", std::string(ld::rules::avatar::standard::bodyPreset));
    }
    bool wantsSheet = document.insertTarget.has_value();
    if (wantsSheet != sheetShown) {
        sheetShown = wantsSheet;
        if (!wantsSheet) chosenClass.reset();
        if (showSheet) showSheet(wantsSheet);
    }
    std::vector<NodePtr> column{document.opened ? ribbon() : projectBar(), ui::divider(colors::border)};
    if (document.opened) {
        column.push_back(geometry([this](Size size) { return workspace(size); }));
    } else column.push_back(projects());
    auto page = vstack(std::move(column), 0);
    if (!document.opened) page = page->background(rectangle(colors::window));
    std::vector<NodePtr> layers{page->disabled(static_cast<bool>(leaving) || helpOpen)};
    if (document.explorerDrag) {
        const auto& drag = *document.explorerDrag;
        std::string label = drag.ids.size() == 1 ? document.displayName(drag.ids[0]) : trn("{0} oggetto", "{0} oggetti", static_cast<long long>(drag.ids.size()));
        layers.push_back(text(label, type::micro)->paddingHorizontal(spacing::tight)->paddingVertical(borders::pixels)->background(rectangle(drag.valid ? colors::accentSurface : colors::canvas))->overlay(strokeRectangle(drag.valid ? colors::accent : colors::danger, 1))->offset(drag.location.x + 12, drag.location.y + 6));
    }
    if (insertOpen && document.opened && !document.playing) {
        layers.push_back(rectangle(Color::clear())->onTap("command.insert.dismiss", [this] {
            insertOpen = false;
            invalidate();
        }));
        layers.push_back(insertMenu());
    }
    if (document.settingsOpen) layers.push_back(settings());
    if (confirmClearTerrain)
        layers.push_back(dialog(tr("Cancellare tutto il terreno?"), tr("Spariscono terreno e acqua. Puoi annullare con ⌘Z."),
                                {{tr("Annulla"), ButtonRole::secondary, [this] {
                                      confirmClearTerrain = false;
                                      invalidate();
                                  }},
                                 {tr("Cancella tutto"), ButtonRole::destructive, [this] {
                                      confirmClearTerrain = false;
                                      document.perform(ld::StudioEdit::clear());
                                      refresh();
                                  }}}));
    if (helpOpen) layers.push_back(helpSheet());
    if (leaving)
        layers.push_back(dialog(tr("Salvare le modifiche a «{0}» prima di uscire?", document.world.title), "",
                                {{tr("Annulla"), ButtonRole::secondary, [this] {
                                      leaving = nullptr;
                                      afterSave = nullptr;
                                      invalidate();
                                  }},
                                 {tr("Non salvare"), ButtonRole::destructive, [this] {
                                      auto proceed = std::move(leaving);
                                      leaving = nullptr;
                                      if (proceed) proceed();
                                      invalidate();
                                  }},
                                 {tr("Salva"), ButtonRole::primary, [this] {
                                      afterSave = std::move(leaving);
                                      leaving = nullptr;
                                      save(false);
                                      invalidate();
                                  }}}));
    return zstack(std::move(layers), Alignment::topLeading())->compact();
}

NodePtr StudioShell::helpSheet() {
    std::vector<std::pair<std::string, std::string>> entries{{tr("Seleziona"), "⌘1"}, {tr("Sposta"), "⌘2"}, {tr("Scala"), "⌘3"}, {tr("Ruota"), "⌘4"}, {tr("Gioca"), "F5"}, {tr("Esegui"), "F8"}, {tr("Ferma"), "⇧F5"}, {tr("Salva"), "⌘S"},
                                                              {tr("Annulla@@modifica"), "⌘Z"}, {tr("Ripeti"), "⇧⌘Z"}, {tr("Taglia"), "⌘X"}, {tr("Copia"), "⌘C"}, {tr("Incolla"), "⌘V"}, {tr("Duplica"), "⌘D"}, {tr("Raggruppa"), "⌘G"}, {tr("Separa"), "⇧⌘U"},
                                                              {tr("Elimina"), "⌫"}, {tr("Rinomina"), "F2"}, {tr("Blocca"), "⌘L"}, {tr("Chiudi progetto"), "⌘W"}};
    std::vector<NodePtr> cells;
    for (const auto& [command, keys] : entries) cells.push_back(hstack({text(command, type::small), spacer(0.0), text(keys, type::smallStrong, colors::textSecondary)}, spacing::s4)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::rowHeight)->overlay(rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels), Alignment::bottom()));
    auto table = columns(std::move(cells), 2, spacing::s8, 0)->id("help.shortcuts");
    auto panel = vstack({text(tr("Scorciatoie da tastiera"), type::dialogTitle), table, hstack({spacer(0.0), button("help.close", tr("Chiudi"), [this] {
                                                                                                    helpOpen = false;
                                                                                                    invalidate();
                                                                                                }, ButtonRole::primary)},
                                                                                              spacing::s4)},
                        spacing::s6, HAlign::leading);
    auto sheet = classicBorder(panel->padding(spacing::s8)->frameSize(metrics::dialogWidth, std::nullopt)->background(rectangle(colors::surface))->clipped(metrics::dialogRadius), metrics::dialogRadius)->background(roundedRectangle(metrics::dialogRadius, Color::black(shadows::opacity))->offset(0, shadows::offset));
    return zstack({rectangle(Color::black(shadows::scrim))->onTap("help.scrim", [] {}), sheet})->foreground(colors::textPrimary)->id("dialog");
}

NodePtr StudioShell::projectBar() {
    std::vector<NodePtr> items{logo(16, "Ludissey Studio")->foreground(colors::textPrimary), spacer(spacing::s6)};
    if (store->busy) items.push_back(loading());
    if (!store->notice.empty()) items.push_back(lineLimited(text(store->notice, type::caption, colors::accent), 1));
    items.push_back(text(store->profile ? store->profile->username : "", type::caption, colors::textSecondary));
    items.push_back(rectangle(colors::border)->frameSize(1, metrics::compactControlHeight));
    items.push_back(button("studio.logout", tr("Esci dall’account"), [this] { store->confirmLogout(); }, ButtonRole::destructive)->disabled(store->busy));
    return hstack(std::move(items), spacing::s4)->paddingHorizontal(spacing::s6)->paddingVertical(spacing::s3)->background(rectangle(colors::surface));
}

NodePtr StudioShell::viewButton(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help) {
    auto result = button(id, ui::icon(icon, iconSize::compact), [this, action] {
                      action();
                      refresh();
                  }, active ? ButtonRole::primary : ButtonRole::dark, ButtonSize::small)
                      ->help(help.empty() ? title : help);
    result->selected = active;
    return enabled ? result : result->disabled();
}

NodePtr StudioShell::commandPicker(const std::string& id, const std::vector<std::string>& options, int selected, double width, std::function<void(int)> choose, const std::string& help) {
    NativeField field;
    field.id = id;
    field.options = options;
    field.selectedOption = selected;
    field.editable = !document.playing;
    field.choose = [this, choose](int index) {
        choose(index);
        refresh();
    };
    return select(field)->frameSize(width, std::nullopt)->help(help);
}

NodePtr StudioShell::pullDown(const std::string& id, NodePtr face, bool enabled, std::function<std::vector<MenuItem>()> items, const std::string& help, std::optional<Color> ink) {
    Surface* surface = Surface::active();
    bool hovered = enabled && surface && surface->hovered(id);
    double height = metrics::compactControlHeight + spacing::s2;
    auto shell = hstack({face, glyph("freccia-giu", tokens::icon::chevron, ink.value_or(enabled ? colors::textSecondary : colors::textDisabled))}, spacing::s2)
                     ->paddingHorizontal(spacing::s3)
                     ->flexible(height, std::nullopt, height, height)
                     ->fixed(true, false)
                     ->background(rectangle(hovered ? colors::hover : Color::clear()))
                     ->clipped(metrics::smallRadius);
    auto result = shell->pullDownMenu(id, [this, items] {
                          auto list = items();
                          for (auto& item : list)
                              if (item.action) item.action = [this, action = item.action] {
                                  action();
                                  refresh();
                              };
                          return list;
                      })->help(help)->id(id);
    return enabled ? result : result->disabled();
}

NodePtr StudioShell::ribbonLarge(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help) {
    Surface* surface = Surface::active();
    bool hovered = enabled && surface && surface->hovered(id), pressed = enabled && surface && surface->pressed(id);
    Font face_ = Font{std::string(tokens::font::family), tokens::studioFont::ribbonLabel, Weight::bold};
    NodePtr name = lineLimited(text(title, face_, enabled ? colors::textPrimary : colors::textDisabled), 1)->fixed(true, false);
    auto face = vstack({ui::icon(icon, iconSize::ribbon), enabled ? embossed(name, emboss::light) : name}, spacing::s1)
                    ->paddingHorizontal(spacing::s3)
                    ->flexible(metrics::ribbonCommand, std::nullopt, std::nullopt, infinity)
                    ->fixed(true, false)
                    ->background(toolWell(active || pressed, hovered))
                    ->opacity(enabled ? 1 : tokens::opacity::disabled);
    auto tapped = plainButton(id, face, [this, action] {
        action();
        refresh();
    });
    tapped->label = title;
    tapped->selected = active;
    auto result = tapped->help(help.empty() ? title : help);
    return enabled ? result : result->disabled();
}

NodePtr StudioShell::ribbonSmall(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help, bool labelled) {
    Surface* surface = Surface::active();
    bool hovered = enabled && surface && surface->hovered(id), pressed = enabled && surface && surface->pressed(id);
    std::vector<NodePtr> parts{ui::icon(icon, iconSize::compact)};
    if (labelled) {
        NodePtr name = lineLimited(text(title, type::small, enabled ? colors::textPrimary : colors::textDisabled), 1)->fixed(true, false);
        parts.push_back(enabled ? embossed(name, emboss::light) : name);
    }
    auto face = hstack(std::move(parts), spacing::s2)
                    ->paddingHorizontal(spacing::s2)
                    ->frameSize(std::nullopt, iconSize::compact + spacing::s2, Alignment::leading())
                    ->fixed(true, false)
                    ->background(toolWell(active || pressed, hovered))
                    ->opacity(enabled ? 1 : tokens::opacity::disabled);
    auto tapped = plainButton(id, face, [this, action] {
        action();
        refresh();
    });
    tapped->label = title;
    tapped->selected = active;
    auto result = tapped->help(help.empty() ? title : help);
    return enabled ? result : result->disabled();
}

NodePtr StudioShell::toolWell(bool sunk, bool hovered) {
    return canvas([sunk, hovered](Canvas& c, const Frame& f) {
        if (sunk) {
            c.fillGradient(f, Gradient(sheen::pressedTool.top, sheen::pressedTool.bottom), metrics::switchRadius);
            c.innerShadow(f, metrics::switchRadius, drops::pressed.dy, drops::pressed.blur, Color::black(shadows::navigationActive));
            c.stroke(f, sheen::pressedTool.border, borders::pixels, metrics::switchRadius);
        } else if (hovered) c.fill(f, Color::white(tokens::opacity::barLight), metrics::switchRadius);
    });
}

NodePtr StudioShell::etched() { return hstack({rectangle(Color::black(tokens::opacity::navigationBottom))->frameSize(borders::pixels, std::nullopt), rectangle(Color::white(tokens::opacity::alertEdge))->frameSize(borders::pixels, std::nullopt)}, 0)->paddingVertical(spacing::s3); }

NodePtr StudioShell::ribbonGroup(const std::string& id, const std::string& title, std::vector<NodePtr> items) {
    Font face{std::string(tokens::font::family), tokens::studioFont::groupTitle, Weight::bold};
    auto name = embossed(lineLimited(text(ld::uppercase(title), face, colors::textTertiary), 1)->fixed(true, false), emboss::light);
    return hstack({vstack({hstack(std::move(items), spacing::s1)->flexible(std::nullopt, std::nullopt, std::nullopt, infinity), name}, spacing::s1)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s2), etched()}, 0)
        ->fixed(true, false)
        ->id("ribbon.group." + id);
}

NodePtr StudioShell::ribbon() {
    return geometry([this](Size size) { return ribbonRow(size.width); })->frameSize(std::nullopt, metrics::ribbonTabs + metrics::ribbonBody)->id("studio.commandBar");
}

NodePtr StudioShell::ribbonRow(double width) {
    bool playing = document.playing;
    bool roomy = width >= breakpoints::narrow;
    auto run = [this](ld::StudioCommand command) { return [this, command] { document.run(command); }; };
    auto can = [this](ld::StudioCommand command) { return document.can(command); };
    auto item = [](std::string title, bool enabled, std::function<void()> action, bool checked = false) {
        MenuItem entry;
        entry.title = std::move(title);
        entry.enabled = enabled;
        entry.action = std::move(action);
        entry.checked = checked;
        return entry;
    };
    auto column = [](std::vector<NodePtr> rows) { return vstack(std::move(rows), 0, HAlign::leading); };
    auto tools = [this, playing] {
        std::vector<NodePtr> out;
        for (auto tool : {ld::StudioTool::select, ld::StudioTool::move, ld::StudioTool::scale, ld::StudioTool::rotate}) {
            int shortcut = tool == ld::StudioTool::select ? 1 : tool == ld::StudioTool::move ? 2 : tool == ld::StudioTool::scale ? 3 : 4;
            out.push_back(ribbonLarge("command.tool." + std::string(ld::toolKey(tool)), std::string(ld::toolSymbol(tool)), ld::toolTitle(tool), !playing, document.tool == tool && !document.terrainEditing, [this, tool] {
                document.tool = tool;
                document.terrainEditing = false;
                document.closeTerrainTool();
            }, ld::toolTitle(tool) + " (⌘" + std::to_string(shortcut) + ")"));
        }
        return out;
    };
    auto playMenu = [this, item, can, roomy] {
        bool enabled = can(ld::StudioCommand::play) || can(ld::StudioCommand::run);
        auto menu = pullDown("command.play.menu", empty()->frameSize(0, 0), enabled, [this, item, can] {
            return std::vector<MenuItem>{item(tr("Gioca (F5)"), can(ld::StudioCommand::play), [this] { document.run(ld::StudioCommand::play); }), item(tr("Gioca da qui"), can(ld::StudioCommand::playHere), [this] { document.run(ld::StudioCommand::playHere); }), MenuItem::divider(),
                                         item(tr("Esegui (F8)"), can(ld::StudioCommand::run), [this] { document.run(ld::StudioCommand::run); })};
        }, tr("Altri modi di provare l’esperienza"), colors::textOnAccent);
        double side = roomy ? metrics::buttonLarge : metrics::button;
        return menu->frameSize(std::nullopt, side)->background(glossBackground(ButtonRole::success, roomy ? metrics::buttonLargeRadius : metrics::buttonRadius))->opacity(enabled ? 1 : tokens::opacity::disabled);
    };
    auto play = [this, can, roomy] {
        auto face = hstack({glyph("gioca", iconSize::toolbar, colors::textOnAccent), text(tr("Gioca"))}, spacing::s2);
        face->label = tr("Gioca");
        auto result = button("command.play", face, [this] {
                          document.run(ld::StudioCommand::play);
                          refresh();
                      }, ButtonRole::success, roomy ? ButtonSize::large : ButtonSize::normal)
                          ->help(tr("Gioca: entra nel mondo col tuo personaggio (F5)"));
        return can(ld::StudioCommand::play) ? result : result->disabled();
    };
    auto stop = [this, can, roomy] {
        auto result = button("command.stop", tr("Ferma"), [this] {
                          document.run(ld::StudioCommand::stop);
                          refresh();
                      }, ButtonRole::dark, roomy ? ButtonSize::large : ButtonSize::normal)
                          ->help(tr("Ferma: torna a costruire (⇧F5)"));
        return can(ld::StudioCommand::stop) ? result : result->disabled();
    };
    bool selected = can(ld::StudioCommand::copy);
    bool locked = can(ld::StudioCommand::unlock);
    auto part = document.selectedPart();
    std::vector<NodePtr> groups;
    if (ribbonTab == "home") {
        groups.push_back(ribbonGroup("clipboard", tr("Appunti"), {ribbonLarge("command.paste", "ld-icon-action-paste", tr("Incolla"), can(ld::StudioCommand::paste), false, run(ld::StudioCommand::paste), tr("Incolla (⌘V)")),
                                                                 column({ribbonSmall("command.copy", "ld-icon-action-copy", tr("Copia"), selected, false, run(ld::StudioCommand::copy), tr("Copia (⌘C)"), roomy), ribbonSmall("command.cut", "ld-icon-action-cut", tr("Taglia"), can(ld::StudioCommand::cut), false, run(ld::StudioCommand::cut), tr("Taglia (⌘X)"), roomy),
                                                                         ribbonSmall("command.duplicate", "ld-icon-action-duplicate", tr("Duplica"), can(ld::StudioCommand::duplicate), false, run(ld::StudioCommand::duplicate), tr("Duplica (⌘D)"), roomy)})}));
        groups.push_back(ribbonGroup("tools", tr("Strumenti"), tools()));
        groups.push_back(ribbonGroup("terrain", tr("Terreno"), {ribbonLarge("command.terrain", "ld-icon-studio-terrain", tr("Editor"), !playing, document.terrainEditing, [this] {
                                                                   document.terrainEditing = !document.terrainEditing;
                                                                   document.closeTerrainTool();
                                                                   if (document.terrainEditing) document.dockTab = "terrain";
                                                               }, tr("Strumenti del terreno"))}));
        groups.push_back(ribbonGroup("insert", tr("Inserisci"), {ribbonLarge("command.insert", "ld-icon-object-part", tr("Parte"), !playing && static_cast<int>(document.world.blocks.size()) < ld::studio_limits::parts, insertOpen, [this] { insertOpen = !insertOpen; }, tr("Inserisci un oggetto nel mondo")),
                                                                ribbonLarge("command.toolbox", "ld-icon-panel-toolbox", tr("Casella strumenti"), true, document.panels.count("toolbox") > 0, [this] { togglePanel("toolbox"); })}));
        groups.push_back(ribbonGroup("edit", tr("Modifica"), {column({ribbonSmall("command.group", "ld-icon-studio-group", tr("Raggruppa"), can(ld::StudioCommand::group), false, run(ld::StudioCommand::group), tr("Raggruppa (⌘G)"), roomy), ribbonSmall("command.ungroup", "ld-icon-studio-ungroup", tr("Separa"), can(ld::StudioCommand::ungroup), false, run(ld::StudioCommand::ungroup), tr("Separa (⇧⌘U)"), roomy)}),
                                                             column({ribbonSmall("command.lock", "ld-icon-action-lock", tr("Blocca"), locked || can(ld::StudioCommand::lock), locked, run(locked ? ld::StudioCommand::unlock : ld::StudioCommand::lock), tr("Blocca o sblocca (⌘L)"), roomy),
                                                                     ribbonSmall("command.anchor", "ld-icon-studio-anchor", tr("Ancora@@parte"), !playing && part.has_value(), part && part->anchored, [this, part] { document.setAnchored(!(part && part->anchored)); }, tr("Ancora: la parte resta ferma"), roomy)})}));
        groups.push_back(ribbonGroup("test", tr("Prova"), {hstack({play(), playMenu()}, borders::pixels)->id("command.test")->paddingHorizontal(spacing::s2), stop()}));
    } else if (ribbonTab == "model") {
        std::vector<double> moves{0.05, 0.25, 1, 2}, angles{1, 15, 45, 90};
        std::vector<std::string> moveTitles, angleTitles;
        int moveIndex = -1, angleIndex = -1;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            moveTitles.push_back(ld::localization::decimal(moves[i], 2));
            if (moves[i] == document.moveSnap) moveIndex = static_cast<int>(i);
        }
        for (std::size_t i = 0; i < angles.size(); ++i) {
            angleTitles.push_back(ld::localization::decimal(angles[i], 0) + "°");
            if (angles[i] == document.angleSnap) angleIndex = static_cast<int>(i);
        }
        auto labelled = [](const std::string& title, NodePtr control) { return hstack({text(title, type::small, colors::textSecondary)->frameSize(metrics::ribbonCommand + spacing::s4, std::nullopt, Alignment::leading()), control}, spacing::s2); };
        groups.push_back(ribbonGroup("tools", tr("Strumenti"), tools()));
        groups.push_back(ribbonGroup("snap", tr("Passo"), {column({labelled(tr("Sposta"), commandPicker("command.snap.position", moveTitles, moveIndex, metrics::ribbonPicker, [this, moves](int i) { document.moveSnap = moves[static_cast<std::size_t>(i)]; }, tr("Passo di spostamento e scala"))),
                                                                  labelled(tr("Ruota"), commandPicker("command.snap.angle", angleTitles, angleIndex, metrics::ribbonPicker, [this, angles](int i) { document.angleSnap = angles[static_cast<std::size_t>(i)]; }, tr("Passo di rotazione")))})}));
        groups.push_back(ribbonGroup("space", tr("Spazio"), {segments("command.space", {tr("Globale"), tr("Locale")}, document.moveSpace == ld::TransformSpace::world ? 0 : 1, [this](int i) {
                                                                document.moveSpace = i == 0 ? ld::TransformSpace::world : ld::TransformSpace::local;
                                                                refresh();
                                                            })->disabled(playing)->help(tr("Assi globali o assi locali per lo strumento Sposta"))}));
        std::vector<NodePtr> parts;
        for (const auto& kind : ld::studio_catalog::primitives())
            parts.push_back(ribbonLarge("command.part." + kind, ld::studio_catalog::symbol(kind), ld::studio_catalog::title(kind), !playing && static_cast<int>(document.world.blocks.size()) < ld::studio_limits::parts, false, [this, kind] { document.addPart(kind); }));
        groups.push_back(ribbonGroup("parts", tr("Parti"), std::move(parts)));
        groups.push_back(ribbonGroup("import", tr("Importa"), {ribbonLarge("command.import", "ld-icon-action-import", std::string(".obj, .fbx"), !playing, false, [this] { importModel(); }, tr("Importa un modello 3D"))}));
        groups.push_back(ribbonGroup("groups", tr("Gruppi"), {ribbonLarge("command.group", "ld-icon-studio-group", tr("Raggruppa"), can(ld::StudioCommand::group), false, run(ld::StudioCommand::group), tr("Raggruppa (⌘G)")), ribbonLarge("command.ungroup", "ld-icon-studio-ungroup", tr("Separa"), can(ld::StudioCommand::ungroup), false, run(ld::StudioCommand::ungroup), tr("Separa (⇧⌘U)"))}));
    } else if (ribbonTab == "test") {
        groups.push_back(ribbonGroup("test", tr("Prova"), {play()->paddingHorizontal(spacing::s2), ribbonLarge("command.playHere", "ld-icon-studio-frame", tr("Gioca da qui"), can(ld::StudioCommand::playHere), false, run(ld::StudioCommand::playHere)),
                                                           ribbonLarge("command.run", "ld-icon-studio-run", tr("Esegui"), can(ld::StudioCommand::run), false, run(ld::StudioCommand::run), tr("Esegui: avvia solo gli script (F8)")), stop()->paddingHorizontal(spacing::s2)}));
    } else {
        std::vector<NodePtr> first, second;
        for (std::size_t i = 0; i < panelList.size(); ++i) {
            std::string key = panelList[i].key;
            auto box = checkbox("command.panel." + key, panelTitle(key), document.panels.count(key) > 0, [this, key](bool) {
                togglePanel(key);
                refresh();
            });
            (i < 3 ? first : second).push_back(box);
        }
        groups.push_back(ribbonGroup("panels", tr("Pannelli"), {vstack(std::move(first), spacing::s1, HAlign::leading), vstack(std::move(second), spacing::s1, HAlign::leading)->padding(Insets{0, spacing::s6, 0, 0})}));
        groups.push_back(ribbonGroup("show", tr("Mostra"), {vstack({checkbox("command.grid", tr("Griglia"), document.showGrid, [this](bool) {
                                                                        toggleView("grid");
                                                                        refresh();
                                                                    }),
                                                                    checkbox("command.gui", tr("Interfaccia di gioco"), document.showGui, [this](bool) {
                                                                        toggleView("gui");
                                                                        refresh();
                                                                    })},
                                                                   spacing::s1, HAlign::leading)}));
    }
    groups.push_back(spacer(0.0));
    std::vector<NodePtr> strip{logo(tokens::studioFont::pageTitle, "Ludissey Studio")->foreground(colors::brand)->paddingHorizontal(spacing::s4)->help("Ludissey Studio " LUDISSEY_VERSION)};
    std::vector<std::string> keys{"home", "model", "test", "view"};
    int current = static_cast<int>(std::find(keys.begin(), keys.end(), ribbonTab) - keys.begin());
    strip.push_back(segments("ribbon.tab", {tr("Home"), tr("Modello"), tr("Prova"), tr("Vista")}, current, [this, keys](int i) {
        ribbonTab = keys[static_cast<std::size_t>(i)];
        insertOpen = false;
        invalidate();
    }, false, keys)->compact(false));
    strip.push_back(spacer(0.0));
    if (playing) strip.push_back(hstack({presence(true, metrics::presence), embossed(text(document.playMode == ld::StudioPlayMode::character ? tr("Prova in corso") : tr("Script in esecuzione"), type::smallStrong, colors::textPrimary), emboss::light)}, spacing::s2)->id("command.playing"));
    if (store->busy) strip.push_back(loading());
    auto quick = [this](const std::string& id, const std::string& symbol, std::function<void()> action, const std::string& help, bool enabled) {
        auto result = button(id, ui::icon(symbol, iconSize::compact), [this, action] {
                          action();
                          invalidate();
                      }, ButtonRole::secondary, ButtonSize::small)
                          ->help(help);
        return enabled ? result : result->disabled();
    };
    strip.push_back(quick("quick.undo", "ld-icon-action-undo", [this] { document.run(ld::StudioCommand::undo); }, tr("Annulla {0} (⌘Z)", document.undoLabel().value_or("")), document.can(ld::StudioCommand::undo)));
    strip.push_back(quick("quick.redo", "ld-icon-action-redo", [this] { document.run(ld::StudioCommand::redo); }, tr("Ripeti {0} (⇧⌘Z)", document.redoLabel().value_or("")), document.can(ld::StudioCommand::redo)));
    strip.push_back(pullDown("command.more", glyph("altro", iconSize::compact, colors::textPrimary), true, [this, item] {
        std::vector<MenuItem> list;
        list.push_back(item(tr("Impostazioni esperienza…"), !document.playing, [this] { document.settingsOpen = true; }));
        list.push_back(item(tr("Salva come…"), !store->busy && !document.playing, [this] { saveAs(); }));
        list.push_back(MenuItem::divider());
        list.push_back(item(tr("Reimposta layout"), true, [this] { resetLayout(); }));
        list.push_back(item(tr("Scorciatoie da tastiera"), true, [this] { showHelp(); }));
        list.push_back(MenuItem::divider());
        list.push_back(item(tr("Chiudi progetto"), !store->busy, [this] { requestClose(); }));
        list.back().shortcut = "⌘W";
        return list;
    }, tr("Altri comandi")));
    strip.push_back(hstack({button("studio.save", tr("Salva"), [this] { save(false); })->disabled(store->busy || playing || !document.dirty())->help(tr("Salva (⌘S)")), button("studio.publish", publishing && store->busy ? tr("Pubblico…") : tr("Pubblica"), [this] { save(true); }, ButtonRole::primary)->disabled(store->busy || playing)->help(tr("Pubblica su Ludissey"))}, spacing::s4)->paddingHorizontal(spacing::s2)->id("ribbon.fixed"));
    return vstack({hstack(std::move(strip), spacing::s4)->paddingHorizontal(spacing::s3)->frameSize(std::nullopt, metrics::ribbonTabs)->fillWidth()->background(sheet(sheen::toolbar)),
                   hstack(std::move(groups), 0)->frameSize(std::nullopt, metrics::ribbonBody)->fillWidth(Alignment::leading())->background(sheet(sheen::ribbon))->clipped()->id("ribbon.body")},
                  0);
}

NodePtr StudioShell::insertMenu() {
    std::vector<NodePtr> rows{text(tr("Parti"), type::caption, colors::textSecondary)->paddingHorizontal(spacing::s3)};
    for (const auto& kind : ld::studio_catalog::primitives()) {
        std::string id = "insert." + kind;
        Surface* surface = Surface::active();
        bool hovered = surface && surface->hovered(id);
        rows.push_back(plainButton(id, hstack({ui::icon(ld::studio_catalog::symbol(kind), iconSize::compact), text(ld::studio_catalog::title(kind), type::small), spacer(0.0)}, spacing::s3)->paddingHorizontal(spacing::s3)->frameSize(std::nullopt, metrics::rowHeight)->fillWidth()->background(rectangle(hovered ? colors::hover : Color::clear())), [this, kind] {
            document.addPart(kind);
            insertOpen = false;
            refresh();
        }));
    }
    rows.push_back(ui::divider());
    rows.push_back(plainButton("insert.object", hstack({ui::icon("ld-icon-action-create", iconSize::compact), text(tr("Altro oggetto…"), type::small), spacer(0.0)}, spacing::s3)->paddingHorizontal(spacing::s3)->frameSize(std::nullopt, metrics::rowHeight)->fillWidth(), [this] {
        auto selection = document.selection();
        document.insertTarget = selection && !selection->service ? selection->id : std::string("Workspace");
        insertOpen = false;
        invalidate();
    }));
    auto card = classicBorder(vstack(std::move(rows), 0, HAlign::leading)->paddingVertical(spacing::s2)->frameSize(200, std::nullopt)->background(rectangle(colors::surface))->clipped(metrics::smallRadius), metrics::smallRadius)
                    ->boxShadow(drops::popover.color, 0, drops::popover.dy, drops::popover.blur, metrics::smallRadius);
    return card->offset(insertAnchor, metrics::ribbonTabs + metrics::ribbonBody)->id("command.insert.menu");
}

NodePtr StudioShell::divider(const std::string& id, bool vertical, std::function<void(double)> change) {
    Surface* surface = Surface::active();
    bool hovered = surface && (surface->hovered(id) || surface->pressed(id));
    auto bar = rectangle(hovered ? colors::accent : colors::border)->frameSize(vertical ? std::optional<double>(4) : std::nullopt, vertical ? std::nullopt : std::optional<double>(4));
    return bar->onDrag(id, [this, vertical, change](const DragEvent& e) {
        change(vertical ? e.delta.x : e.delta.y);
        invalidate();
    });
}

NodePtr StudioShell::workspace(Size available) {
    const double minimumCenter = std::max(360.0, std::round(available.width * 0.42)), minimumDock = 180, minimumSide = 220, minimumTop = 260, minimumUtility = 120, minimumPane = 120, dividerWidth = 4;
    bool terrainOpen = document.terrainEditing && !document.playing;
    if (!terrainOpen && document.dockTab == "terrain") document.dockTab = "toolbox";
    if (terrainOpen && !document.panels.count("toolbox")) document.dockTab = "terrain";
    bool wantsDock = document.panels.count("toolbox") > 0 || terrainOpen;
    bool wantsExplorer = document.panels.count("explorer") > 0, wantsProperties = document.panels.count("properties") > 0;
    bool wantsSide = wantsExplorer || wantsProperties;
    std::vector<std::string> tabs;
    for (const auto& name : utilities)
        if (document.panels.count(name)) tabs.push_back(name);
    if (!tabs.empty() && std::find(tabs.begin(), tabs.end(), document.utilityTab) == tabs.end()) document.utilityTab = tabs.front();
    double dividers = (wantsDock ? dividerWidth : 0) + (wantsSide ? dividerWidth : 0);
    double dockWidth = wantsDock ? std::clamp(document.dockWidth, minimumDock, metrics::panelMaximum) : 0;
    double sideWidth = wantsSide ? std::clamp(document.propertiesWidth, minimumSide, metrics::panelMaximum) : 0;
    double sideBudget = std::max(0.0, available.width - minimumCenter - dividers);
    if (dockWidth + sideWidth > sideBudget) {
        double floor = (wantsDock ? minimumDock : 0) + (wantsSide ? minimumSide : 0);
        double spare = std::max(0.0, sideBudget - floor), extra = (dockWidth + sideWidth) - floor;
        double ratio = extra > 0 ? std::min(1.0, spare / extra) : 0;
        if (wantsDock) dockWidth = std::round(minimumDock + (dockWidth - minimumDock) * ratio);
        if (wantsSide) sideWidth = std::round(minimumSide + (sideWidth - minimumSide) * ratio);
    }
    bool hasUtility = !tabs.empty();
    double strip = metrics::tabHeight;
    bool collapsed = hasUtility && (document.utilityCollapsed || available.height - strip - dividerWidth < minimumTop + minimumUtility);
    double utilityHeight = !hasUtility ? 0 : collapsed ? strip : std::clamp(document.utilityHeight, minimumUtility, std::max(minimumUtility, available.height - minimumTop - dividerWidth));
    double topHeight = available.height - (hasUtility ? utilityHeight + (collapsed ? borders::pixels : dividerWidth) : 0);
    auto clampDock = [=](double v) { return std::clamp(v, minimumDock, std::max(minimumDock, std::min(metrics::panelMaximum, available.width - sideWidth - minimumCenter - dividers))); };
    auto clampSide = [=](double v) { return std::clamp(v, minimumSide, std::max(minimumSide, std::min(metrics::panelMaximum, available.width - dockWidth - minimumCenter - dividers))); };
    auto clampUtility = [=](double v) { return std::clamp(v, minimumUtility, std::max(minimumUtility, available.height - minimumTop - dividerWidth)); };
    std::vector<NodePtr> columns;
    if (wantsDock) {
        columns.push_back(dock()->frameSize(dockWidth, std::nullopt));
        columns.push_back(divider("divider.dock", true, [this, clampDock](double d) { document.dockWidth = clampDock(document.dockWidth + d); }));
    }
    columns.push_back(center()->fill());
    if (wantsSide) {
        columns.push_back(divider("divider.properties", true, [this, clampSide](double d) { document.propertiesWidth = clampSide(document.propertiesWidth - d); }));
        NodePtr side;
        if (wantsExplorer && wantsProperties) {
            double usable = std::max(minimumPane * 2, topHeight - dividerWidth);
            double explorerHeight = std::clamp(std::round(usable * document.explorerShare), minimumPane, usable - minimumPane);
            side = vstack({explorer()->frameSize(std::nullopt, explorerHeight), divider("divider.explorer", false, [this, usable](double d) { document.explorerShare = std::clamp(document.explorerShare + d / usable, 0.15, 0.85); }), properties()->fill()}, 0);
        } else side = wantsExplorer ? explorer() : properties();
        columns.push_back(side->frameSize(sideWidth, std::nullopt));
    }
    std::vector<NodePtr> rows{hstack(std::move(columns), 0)->fill()};
    if (hasUtility) {
        if (collapsed) rows.push_back(ui::divider());
        else rows.push_back(divider("divider.utility", false, [this, clampUtility](double d) { document.utilityHeight = clampUtility(document.utilityHeight - d); }));
        rows.push_back(utilityArea(utilityHeight, collapsed, tabs)->frameSize(std::nullopt, utilityHeight));
    }
    return vstack(std::move(rows), 0)->fill();
}

NodePtr StudioShell::panelTab(const std::string& id, NodePtr label, bool active, std::function<void()> choose) {
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered(id);
    auto face = headerText(label)->opacity(active || hovered ? 1 : tokens::opacity::secondaryValue)
                    ->paddingHorizontal(spacing::s6)
                    ->frameSize(std::nullopt, metrics::panelHeader)
                    ->background(active ? recess() : rectangle(Color::clear()));
    auto tab = plainButton(id, face, [this, choose] {
        choose();
        invalidate();
    }, Cursor::pointer);
    tab->selected = active;
    return tab;
}

NodePtr StudioShell::dock() {
    std::vector<std::string> tabs;
    if (document.panels.count("toolbox")) tabs.push_back("toolbox");
    if (document.terrainEditing && !document.playing) tabs.push_back("terrain");
    if (std::find(tabs.begin(), tabs.end(), document.dockTab) == tabs.end() && !tabs.empty()) document.dockTab = tabs.front();
    if (tabs.size() == 1 && tabs.front() != "terrain") return vstack({panelHeader(panelTitle(tabs.front()), std::nullopt, "")->id("dock.header"), toolbox()->fill()}, 0)->background(rectangle(colors::surface))->id("studio.dock");
    std::vector<NodePtr> strip;
    for (const auto& name : tabs) {
        std::string id = "dock.tab." + name;
        bool on = document.dockTab == name;
        strip.push_back(panelTab(id, text(panelTitle(name), type::smallStrong), on, [this, name] {
            if (name != "terrain") document.closeTerrainTool();
            document.dockTab = name;
            refresh();
        }));
    }
    strip.push_back(spacer(0.0));
    if (document.dockTab == "terrain")
        strip.push_back(plainButton("dock.close", glyph("chiudi", iconSize::compact, colors::textOnAccent)->frameSize(metrics::panelHeader, metrics::panelHeader), [this] {
                            document.closeTerrainTool();
                            document.terrainEditing = false;
                            refresh();
                        })->help(tr("Chiudi il terreno")));
    NodePtr content = document.dockTab == "terrain" ? terrainPanel() : toolbox();
    return vstack({hstack(std::move(strip), 0)->paddingHorizontal(spacing::s1)->fillWidth()->background(sheet(sheen::panelHeader)), content->fill()}, 0)->background(rectangle(colors::surface))->id("studio.dock");
}

NodePtr StudioShell::utilityArea(double height, bool collapsed, const std::vector<std::string>& tabs) {
    std::vector<NodePtr> strip;
    for (const auto& name : tabs) {
        auto info = std::find_if(panelList.begin(), panelList.end(), [&](const PanelInfo& p) { return p.key == name; });
        std::string id = "utility.tab." + name;
        bool active = document.utilityTab == name && !collapsed;
        std::vector<NodePtr> parts{ui::icon(info->symbol, iconSize::compact), text(panelTitle(name), type::smallStrong)};
        if (name == "output") {
            auto [errors, warnings] = outputCounts();
            if (errors) parts.push_back(counter(static_cast<int>(errors))->help(trn("{0} errore nell’Output", "{0} errori nell’Output", errors))->id("output.errors"));
            if (warnings) parts.push_back(counter(static_cast<int>(warnings), true)->help(trn("{0} avviso nell’Output", "{0} avvisi nell’Output", warnings))->id("output.warnings"));
        }
        strip.push_back(panelTab(id, hstack(std::move(parts), spacing::s2), active, [this, name] {
            if (document.utilityTab == name && !document.utilityCollapsed) document.utilityCollapsed = true;
            else {
                document.utilityTab = name;
                document.utilityCollapsed = false;
                if (name == "toolbox" && document.assets.empty()) document.loadAssets();
            }
            invalidate();
        }));
    }
    strip.push_back(spacer(0.0));
    strip.push_back(plainButton("utility.collapse", glyph(collapsed ? "freccia-su" : "freccia-giu", tokens::icon::chevron, colors::textOnAccent)->frameSize(metrics::panelHeader, metrics::panelHeader), [this] {
                        document.utilityCollapsed = !document.utilityCollapsed;
                        invalidate();
                    })->help(collapsed ? tr("Mostra il pannello") : tr("Riduci il pannello")));
    std::vector<NodePtr> parts{hstack(std::move(strip), 0)->paddingHorizontal(spacing::s1)->background(sheet(sheen::panelHeader))};
    if (!collapsed) {
        NodePtr content = document.utilityTab == "output" ? output() : document.utilityTab == "performance" ? performance() : document.utilityTab == "api" ? apiBrowser() : toolbox();
        parts.push_back(content->fill());
    }
    return vstack(std::move(parts), 0)->background(rectangle(colors::surface))->id("studio.utility");
}

std::string StudioShell::iconFor(const std::string& id) const {
    if (auto block = document.world.block(id)) return block->kindSymbol();
    if (auto name = document.className(id))
        if (auto descriptor = document.registry.find(*name)) return descriptor->icon;
    return "ld-icon-object-part";
}

NodePtr StudioShell::explorerRow(const std::string& id, const std::string& icon, const std::string& title, const std::optional<std::string>& detail, int indent, bool selected, bool expanded, bool hasChildren, bool locked,
                                 std::function<void()> toggle) {
    auto toggleButton = plainButton("explorer.toggle." + id, ui::icon(expanded ? "ld-icon-studio-disclosure-open" : "ld-icon-studio-disclosure-closed", 11)->frameSize(11, 11)->opacity(hasChildren ? 1 : 0), [this, toggle] {
        toggle();
        invalidate();
    });
    if (!hasChildren) toggleButton = toggleButton->disabled();
    bool lit = selected && explorerActive;
    std::vector<NodePtr> label{ui::icon(icon, iconSize::panel), lit ? embossed(lineLimited(text(title, type::smallStrong, colors::textOnAccent), 1), emboss::dark) : lineLimited(text(title, type::small), 1)};
    if (locked) label.push_back(ui::icon("ld-icon-action-lock", iconSize::compact - 4)->opacity(0.7)->help(tr("Bloccato: non si seleziona cliccando nel mondo")));
    label.push_back(spacer(0.0));
    if (detail) label.push_back(text(*detail, type::micro, lit ? colors::textOnAccent : colors::textTertiary));
    bool virtualNode = !document.exists(id);
    auto content = hstack(std::move(label), spacing::s2)->contextMenu([this, id] { return menu(id); });
    content->interaction.id = "explorer.row." + id;
    content->interaction.press = [this, id, virtualNode](Point) {
        explorerActive = true;
        Surface* surface = Surface::active();
        ui::Modifiers held = surface ? surface->modifiers() : ui::Modifiers{};
        if (virtualNode) {
            document.setSelection(ld::StudioSelection::ofService(id));
            document.activeScript.reset();
        } else if (held.command || held.shift) document.extendSelection(id);
        else {
            document.setSelection(ld::StudioSelection::instance(id));
            document.activeScript.reset();
        }
        invalidate();
    };
    if (!virtualNode) {
        content->interaction.doubleTap = [this, id] {
            if (document.isScript(id)) document.openScript(id);
            else {
                document.renaming = id;
                invalidate();
            }
        };
        content->interaction.drag = [this, id](const DragEvent& e) {
            auto selected = document.selectedParts();
            std::vector<std::string> ids = std::find(selected.begin(), selected.end(), id) != selected.end() ? document.selectedIds() : std::vector<std::string>{id};
            document.dragExplorer(ids, {e.location.x, e.location.y});
            if (e.ended) document.dropExplorer();
            invalidate();
        };
    }
    bool target = document.explorerDrag && document.explorerDrag->target == id;
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered("explorer.row." + id);
    Color outline = target ? (document.explorerDrag->valid ? colors::accent : colors::danger) : Color::clear();
    NodePtr fill = lit ? sheet(sheen::selection) : selected ? rectangle(colors::selectionInactive) : hovered ? rectangle(colors::hover) : rectangle(Color::clear());
    return hstack({spacer(0.0)->frameSize(indent * iconSize::compact + spacing::s2, std::nullopt), toggleButton, content}, spacing::s1)
        ->foreground(colors::textPrimary)
        ->frameSize(std::nullopt, metrics::explorerRow)
        ->fillWidth()
        ->background(fill)
        ->padding(Insets{0, 0, 0, spacing::s3})
        ->overlay(strokeRectangle(outline, borders::pixels))
        ->id("explorer.target." + id);
}

NodePtr StudioShell::serviceNode(const ld::ServiceDescriptor& service) {
    auto children = document.children(service.name);
    bool terrain = service.name == "Workspace" && document.world.terrain.present();
    std::size_t count = children.size() + (terrain ? 1 : 0);
    bool expanded = document.expandedServices.count(service.name) > 0;
    bool selected = document.selection() == std::optional<ld::StudioSelection>(ld::StudioSelection::ofService(service.name));
    std::vector<NodePtr> rows{explorerRow(service.name, service.icon, serviceTitle(service.name), count == 0 ? std::nullopt : std::optional<std::string>(std::to_string(count)), 0, selected, expanded, count > 0, false, [this, name = service.name] {
                                  if (document.expandedServices.count(name)) document.expandedServices.erase(name);
                                  else document.expandedServices.insert(name);
                              })->help(serviceHelp(service.name))->id("explorer.service." + service.name)};
    if (expanded && terrain) {
        bool chosen = document.selection() == std::optional<ld::StudioSelection>(ld::StudioSelection::ofService("Terrain"));
        rows.push_back(explorerRow("Terrain", "ld-icon-studio-terrain", "Terrain", std::nullopt, 1, chosen, false, false, false, [] {})->help(tr("Il terreno e l’acqua del mondo."))->id("explorer.terrain"));
    }
    if (expanded)
        for (const auto& child : children) rows.push_back(node(child, 1));
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr StudioShell::node(const std::string& id, int indent) {
    auto children = document.children(id);
    bool open = document.expandedIds.count(id) > 0;
    std::vector<NodePtr> rows;
    if (document.renaming == id) {
        if (renameFor != id) {
            renameFor = id;
            renameDraft = document.displayName(id);
        }
        auto commit = [this, id] {
            document.rename(id, renameDraft);
            document.renaming.reset();
            renameFor.reset();
            invalidate();
        };
        NativeField field;
        field.id = "explorer.rename";
        field.prompt = tr("Nome");
        field.text = renameDraft;
        field.font = type::small;
        field.color = colors::textPrimary;
        field.changed = [this](const std::string& value) { renameDraft = value; };
        field.commit = [commit](const std::string&) { commit(); };
        rows.push_back(hstack({spacer(0.0)->frameSize(20, std::nullopt), ui::textField(field), plainButton("explorer.rename.ok", text("OK", type::micro, colors::accent), commit),
                               plainButton("explorer.rename.cancel", text(tr("Annulla"), type::micro, colors::textSecondary), [this] {
                                   document.renaming.reset();
                                   renameFor.reset();
                                   invalidate();
                               })},
                              spacing::s2)
                           ->paddingHorizontal(spacing::compact)
                           ->frameSize(std::nullopt, metrics::compactControlHeight)
                           ->background(rectangle(colors::accentSurface)));
    } else {
        auto block = document.world.block(id);
        auto selected = document.selectedParts();
        rows.push_back(explorerRow(id, iconFor(id), document.displayName(id), children.empty() ? std::nullopt : std::optional<std::string>(std::to_string(children.size())), indent, std::find(selected.begin(), selected.end(), id) != selected.end(), open,
                                   !children.empty(), block && block->locked, [this, id] {
                                       if (document.expandedIds.count(id)) document.expandedIds.erase(id);
                                       else document.expandedIds.insert(id);
                                   })
                           ->id("explorer.part." + document.displayName(id)));
    }
    if (open)
        for (const auto& child : children) rows.push_back(node(child, indent + 1));
    return vstack(std::move(rows), 0, HAlign::leading);
}

std::vector<MenuItem> StudioShell::menu(const std::string& id) {
    bool instance = document.exists(id);
    auto selected = document.selectedParts();
    if (instance && std::find(selected.begin(), selected.end(), id) == selected.end()) document.setSelection(ld::StudioSelection::instance(id));
    if (!instance) document.setSelection(ld::StudioSelection::ofService(id));
    invalidate();
    bool editable = !document.playing;
    auto add = [this](const std::string& title, bool enabled, std::function<void()> action, const std::string& icon = "") {
        MenuItem item;
        item.title = title;
        item.icon = icon;
        item.enabled = enabled;
        item.action = [this, action] {
            action();
            invalidate();
        };
        return item;
    };
    auto classes = document.insertable(id);
    MenuItem insert;
    insert.title = tr("Inserisci oggetto");
    insert.icon = "ld-icon-action-create";
    std::string categoryName;
    for (const auto* descriptor : classes) {
        if (descriptor->category != categoryName) {
            if (!categoryName.empty()) insert.submenu.push_back(MenuItem::divider());
            categoryName = descriptor->category;
        }
        insert.submenu.push_back(add(descriptor->className, editable, [this, name = descriptor->className, id] { document.insert(name, id); }, descriptor->icon));
    }
    insert.enabled = editable && !classes.empty();
    std::vector<MenuItem> items{insert, add(tr("Cerca oggetto da inserire…"), editable && !classes.empty(), [this, id] {
                                            document.insertQuery.clear();
                                            document.insertTarget = id;
                                        }),
                                MenuItem::divider()};
    auto command = [&](ld::StudioCommand c, const std::string& icon, const std::string& shortcut) {
        auto item = add(ld::commandTitle(c), document.can(c), [this, c] { document.run(c); }, icon);
        item.shortcut = shortcut;
        return item;
    };
    if (instance) {
        items.push_back(command(ld::StudioCommand::cut, "ld-icon-action-cut", "⌘X"));
        items.push_back(command(ld::StudioCommand::copy, "ld-icon-action-copy", "⌘C"));
    }
    items.push_back(add(tr("Incolla dentro"), editable && !ld::StudioDocument::clipboard().empty(), [this] { document.pasteInto(); }, "ld-icon-action-paste"));
    if (instance) {
        items.push_back(command(ld::StudioCommand::duplicate, "ld-icon-action-duplicate", "⌘D"));
        items.push_back(add(tr("Rinomina"), document.canEdit(id, ld::StudioAction::rename), [this, id] {
            document.setSelection(ld::StudioSelection::instance(id));
            document.renaming = id;
        }));
        items.back().shortcut = "F2";
        items.push_back(MenuItem::divider());
        items.push_back(command(ld::StudioCommand::group, "ld-icon-studio-group", "⌘G"));
        items.push_back(command(ld::StudioCommand::ungroup, "ld-icon-studio-ungroup", "⇧⌘U"));
    }
    items.push_back(add(tr("Seleziona figli"), !document.world.childIds(id).empty(), [this] { document.selectChildren(); }));
    if (document.isScript(id)) {
        items.push_back(MenuItem::divider());
        items.push_back(add(tr("Apri script"), true, [this, id] { document.openScript(id); }, "ld-icon-service-scripts"));
    }
    if (auto block = document.world.block(id)) {
        bool locked = block->locked, anchored = block->anchored;
        items.push_back(MenuItem::divider());
        items.push_back(add(locked ? tr("Sblocca") : tr("Blocca"), editable, [this, locked] { document.run(locked ? ld::StudioCommand::unlock : ld::StudioCommand::lock); }, "ld-icon-action-lock"));
        items.back().shortcut = "⌘L";
        if (block->kind != "model") items.push_back(add(anchored ? tr("Togli ancoraggio") : tr("Ancora: la fisica non la sposta"), editable, [this, anchored] { document.setAnchored(!anchored); }, "ld-icon-studio-anchor"));
    }
    if (instance) {
        items.push_back(MenuItem::divider());
        items.push_back(command(ld::StudioCommand::remove, "ld-icon-action-trash", "⌫"));
    }
    return items;
}

std::string StudioShell::serviceTitle(const std::string& name) const {
    static const std::map<std::string, std::string> aliases = [] {
        std::map<std::string, std::string> out;
        for (auto context : {ld::ScriptContext::server, ld::ScriptContext::client})
            for (const auto& [global, service] : ld::EngineTypes::shared().serviceGlobals(context)) out.emplace(service, global);
        out.emplace("ReplicatedFirst", "Preload");
        return out;
    }();
    auto found = aliases.find(name);
    return found == aliases.end() ? name : found->second;
}

std::string StudioShell::serviceHelp(const std::string& name) const {
    std::string about = name == "Workspace" ? tr("Tutto quello che c’è nel mondo di gioco.")
                        : name == "Players" ? tr("I giocatori in partita.")
                        : name == "Lighting" ? tr("Luce, cielo e nebbia del mondo.")
                        : name == "ReplicatedFirst" ? tr("Oggetti caricati prima del mondo.")
                        : name == "ReplicatedStorage" ? tr("Oggetti e moduli condivisi fra server e giocatori.")
                        : name == "ServerScriptService" ? tr("Script eseguiti solo sul server.")
                        : name == "ServerStorage" ? tr("Oggetti visibili solo al server.")
                        : name == "StarterGui" ? tr("L’interfaccia che ogni giocatore riceve quando entra.")
                        : name == "StarterPack" ? tr("Strumenti dati a ogni giocatore.")
                        : name == "StarterPlayer" ? tr("Script e impostazioni del personaggio di ogni giocatore.")
                        : name == "SoundService" ? tr("Suoni e musica dell’esperienza.")
                        : name == "Teams" ? tr("Squadre dei giocatori.")
                                          : std::string();
    std::string title = serviceTitle(name);
    std::string scripts = title == "Preload" ? tr("Non accessibile dagli script.") : tr("Negli script: {0}", title);
    return about.empty() ? scripts : about + "\n" + scripts;
}

std::string StudioShell::shownName(const std::string& id) const { return document.exists(id) ? document.displayName(id) : serviceTitle(id); }

NodePtr StudioShell::explorer() {
    std::vector<NodePtr> services;
    for (const auto& service : document.registry.services()) services.push_back(serviceNode(service));
    auto list = scroll("explorer.list", vstack(std::move(services), 0, HAlign::leading)->fillWidth(Alignment::leading())->paddingVertical(spacing::s2));
    auto selection = document.selection();
    std::string target = selection ? selection->id : std::string("Workspace");
    bool canInsert = !document.playing && !document.insertable(target).empty();
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered("explorer.insert");
    auto insert = plainButton("explorer.insert", ui::icon("ld-icon-action-create", iconSize::compact)->frameSize(metrics::compactControlHeight, metrics::compactControlHeight)->background(rectangle(hovered ? colors::hover : Color::clear()))->clipped(metrics::smallRadius), [this, target] {
                      document.insertQuery.clear();
                      document.insertTarget = target;
                      invalidate();
                  })->help(tr("Inserisci un oggetto in {0}", shownName(target)));
    auto actions = pullDown("explorer.actions", glyph("altro", iconSize::compact, colors::textOnAccent), selection.has_value(), [this, target] { return menu(target); }, tr("Azioni sulla selezione"), colors::textOnAccent);
    std::string count = trn("{0} oggetto", "{0} oggetti", static_cast<long long>(document.world.blocks.size() + document.world.instances.size()));
    auto header = hstack({headerText(text(panelTitle("explorer"), type::smallStrong)), headerText(text(count, type::caption)), spacer(0.0), canInsert ? insert : insert->disabled(), actions, panelClose("explorer")}, spacing::s2)
                      ->paddingHorizontal(spacing::s4)
                      ->padding(Insets{0, spacing::s1, 0, 0})
                      ->frameSize(std::nullopt, metrics::panelHeader)
                      ->fillWidth()
                      ->background(sheet(sheen::panelHeader));
    return vstack({header, list}, 0)->background(rectangle(colors::surface));
}

void StudioShell::rendered(const Surface& surface) {
    std::map<std::string, ld::Rect> frames;
    for (const auto& element : surface.matching("explorer.target.")) frames[element.id.substr(std::string("explorer.target.").size())] = ld::Rect::make(element.frame.x, element.frame.y, element.frame.width, element.frame.height);
    if (!(frames == document.explorerFrames)) document.explorerFrames = std::move(frames);
    if (auto anchor = surface.find("command.insert")) insertAnchor = anchor->frame.x;
    if (stage)
        for (const auto& slot : surface.viewports())
            if (slot.id == "studio.viewport") stage->size = {slot.frame.width, slot.frame.height};
}

NodePtr StudioShell::insertSheet() {
    std::string target = document.insertTarget.value_or("Workspace");
    auto classes = document.insertable(target);
    auto commit = [this, target](std::optional<std::string> className) {
        if (!className) return;
        document.insertTarget.reset();
        document.insert(*className, target);
        invalidate();
    };
    std::vector<NodePtr> rows;
    for (const auto* descriptor : classes) {
        bool chosen = chosenClass == descriptor->className;
        auto label = hstack({icon(descriptor->icon, iconSize::compact), text(classLabel(descriptor->className), type::small), text(descriptor->className, type::micro, chosen ? Color::white() : colors::textTertiary), spacer(0.0), text(categoryLabel(descriptor->category), type::micro, chosen ? Color::white() : colors::textSecondary)}, spacing::compact)
                         ->foreground(chosen ? Color::white() : colors::textPrimary)
                         ->paddingHorizontal(spacing::panel)
                         ->frameSize(std::nullopt, metrics::rowHeight)
                         ->fillWidth()
                         ->background(rectangle(chosen ? colors::accent : Color::clear()));
        auto rowNode = plainButton("insert.class." + descriptor->className, label, [this, name = descriptor->className] {
            chosenClass = name;
            invalidate();
        });
        rowNode->interaction.doubleTap = [commit, name = descriptor->className] { commit(name); };
        rows.push_back(rowNode);
    }
    if (classes.empty()) rows.push_back(text(tr("Nessun oggetto può essere inserito qui."), type::small, colors::textSecondary)->padding(spacing::panel));
    NativeField search;
    search.id = "studio.insertObject.search";
    search.prompt = tr("Cerca un oggetto");
    search.text = document.insertQuery;
    search.changed = [this](const std::string& value) {
        document.insertQuery = value;
        invalidate();
    };
    search.commit = [this, commit, classes](const std::string&) { commit(chosenClass ? chosenClass : classes.empty() ? std::nullopt : std::optional<std::string>(classes.front()->className)); };
    search.cancel = [this] {
        document.insertTarget.reset();
        invalidate();
    };
    auto header = hstack({text(tr("Inserisci oggetto in {0}", shownName(target)), type::strong), spacer(0.0), text(trn("{0} tipo", "{0} tipi", static_cast<long long>(classes.size())), type::caption, colors::textSecondary)}, spacing::inlineGap)->padding(spacing::panel);
    auto footer = hstack({spacer(), button("insert.cancel", tr("Annulla"), [this] {
                                                   document.insertTarget.reset();
                                                   invalidate();
                                               }),
                          button("insert.confirm", tr("Inserisci"), [this, commit] { commit(chosenClass); }, ButtonRole::primary)->disabled(!chosenClass)})
                      ->padding(spacing::panel);
    return vstack({header, styledField(search)->paddingHorizontal(spacing::panel), ui::divider()->padding(Insets{spacing::panel, 0, 0, 0}), scroll("insert.list", vstack(std::move(rows), 0, HAlign::leading)->fillWidth(Alignment::leading()))->fill(), ui::divider(), footer}, 0, HAlign::leading)
        ->font(type::body)
        ->foreground(colors::textPrimary)
        ->background(rectangle(colors::surface));
}

NodePtr StudioShell::centerTab(const std::string& id, const std::string& title, bool active, std::function<void()> close, std::function<void()> select) {
    Surface* surface = Surface::active();
    bool hovered = !active && surface && surface->hovered("center.tab." + id);
    std::vector<NodePtr> items{plainButton("center.tab." + id, lineLimited(text(title, active ? type::smallStrong : type::small, active || hovered ? colors::textPrimary : colors::textSecondary), 1), [this, select] {
        select();
        invalidate();
    })};
    if (close) items.push_back(plainButton("center.close." + id, glyph("chiudi", iconSize::compact, colors::textTertiary)->frameSize(iconSize::compact, iconSize::compact), [this, close] {
                                   close();
                                   invalidate();
                               })->help(tr("Chiudi scheda")));
    return hstack(std::move(items), spacing::s3)
        ->paddingHorizontal(spacing::s6)
        ->frameSize(std::nullopt, metrics::tabHeight)
        ->background(rectangle(active ? colors::surface : hovered ? Color::white(tokens::opacity::barLight) : Color::clear()))
        ->overlay(rectangle(sheen::toolbar.border)->frameSize(borders::pixels, std::nullopt), Alignment::trailing());
}

NodePtr StudioShell::center() {
    std::vector<NodePtr> tabs{centerTab("viewport", document.world.title, !document.activeScript, nullptr, [this] { document.activeScript.reset(); })};
    for (const auto& id : document.visibleScripts()) {
        auto parent = document.world.parentOf(id);
        std::string parentName = parent ? document.displayName(*parent) : "";
        auto tab = centerTab(id, document.displayName(id), document.activeScript == id, [this, id] { document.closeScript(id); }, [this, id] {
            document.activeScript = id;
            document.setSelection(ld::StudioSelection::instance(id));
        });
        tabs.push_back(parentName.empty() ? tab : tab->help(tr("In {0}", parentName)));
    }
    tabs.push_back(spacer(0.0));
    std::vector<NodePtr> layers;
    if (!document.activeScript) {
        layers.push_back(viewport("studio.viewport"));
        if (!document.playing)
            layers.push_back(hstack({spacer(0.0),
                                     hstack({viewButton("view.grid", "ld-icon-service-workspace", tr("Griglia"), true, document.showGrid, [this] { document.showGrid = !document.showGrid; }, tr("Mostra la griglia del Baseplate")),
                                             viewButton("view.gui", "ld-icon-service-gui", tr("Interfaccia"), true, document.showGui, [this] { document.showGui = !document.showGui; }, tr("Mostra l’interfaccia di gioco (StarterGui)")),
                                             viewButton("view.frame", "ld-icon-studio-frame", tr("Inquadra"), document.selection().has_value(), false, [this] {
                                                 if (stage) stage->frameSelection();
                                             }, tr("Inquadra la selezione (F)"))},
                                            spacing::s3)
                                         ->padding(spacing::s4)},
                                    0)
                                 ->fillWidth(Alignment::top())
                                 ->id("view.options"));
        if (stage && stage->marquee) {
            auto m = *stage->marquee;
            layers.push_back(rectangle(colors::accent.opacity(0.12))->overlay(strokeRectangle(colors::accent, 1))->frameSize(m.width, m.height)->offset(m.x, m.y));
        }
        if (document.showGui) {
            GuiActions actions;
            if (auto session = document.session()) {
                actions.activate = [session](const std::string& id) { session->activate(id); };
                actions.commit = [session](const std::string& id, const std::string& value) { session->commit(id, value); };
                actions.scroll = [session](const std::string& id, double amount) { session->scroll(id, amount); };
            }
            layers.push_back(overlay.build(document.screens(), actions, document.playing)->id("studio.gui"));
        }
    } else {
        auto visible = document.visibleScripts();
        if (std::find(visible.begin(), visible.end(), *document.activeScript) != visible.end()) layers.push_back(scriptPane(*document.activeScript));
    }
    std::vector<NodePtr> stack{hstack(std::move(tabs), 0)->background(sheet(sheen::toolbar))};
    stack.push_back(zstack(std::move(layers), Alignment::topLeading())->fill());
    return vstack(std::move(stack), 0);
}

NodePtr StudioShell::scriptPane(const std::string& id) {
    auto record = document.world.record(id);
    if (!record) return empty();
    std::string className = record->className;
    auto descriptor = document.registry.find(className);
    std::string where = document.path(id);
    if (auto dot = where.find('.'); dot != std::string::npos) where = serviceTitle(where.substr(0, dot)) + where.substr(dot);
    std::vector<NodePtr> header{icon(descriptor ? descriptor->icon : "ld-icon-service-scripts", iconSize::compact), text(record->name, type::smallStrong), lineLimited(text(where, type::caption, colors::textTertiary), 1, Truncation::head)->help(tr("Percorso dello script nel mondo: {0}", where)), spacer(0.0)};
    auto problems = document.scriptErrors();
    if (auto found = problems.find(id); found != problems.end()) {
        int line = found->second.line;
        header.push_back(plainButton("studio.script.error", hstack({ui::icon("ld-icon-status-warning", 13), lineLimited(text(tr("Riga {0}: {1}", line, found->second.message), type::caption, colors::danger), 1)}, spacing::s1), [this, line] {
            document.focusLine = line;
            invalidate();
        }));
    }
    header.push_back(button("studio.find", tr("Trova"), [this] { finder = FindAction::find; })->fixed()->help(tr("Trova nello script (⌘F)")));
    header.push_back(button("studio.replace", tr("Sostituisci"), [this] { finder = FindAction::replace; })->fixed()->disabled(document.playing)->help(tr("Trova e sostituisci (⌥⌘F)")));
    header.push_back(text(std::to_string(ld::utf8Length(record->source())) + "/" + std::to_string(ld::rules::instances::sourceCharacters), type::caption, colors::textTertiary)->help(tr("Caratteri usati e limite dello script")));
    NativeField editor;
    editor.id = "studio.script." + id;
    editor.code = true;
    editor.multiline = true;
    editor.text = record->source();
    editor.editable = !document.playing;
    editor.limit = static_cast<std::size_t>(ld::rules::instances::sourceCharacters);
    if (auto found = problems.find(id); found != problems.end()) editor.errorLine = found->second.line;
    editor.revealLine = document.focusLine;
    editor.find = finder;
    editor.highlight = highlightLim;
    if (auto points = document.breakpoints.find(id); points != document.breakpoints.end()) editor.breakpoints = points->second;
    lim::AnalyzeOptions options;
    options.context = className == "LocalScript" ? lim::ExecutionContext::client : className == "ModuleScript" ? lim::ExecutionContext::shared : lim::ExecutionContext::server;
    options.file = record->name;
    options.extraGlobals.push_back({"script", lim::Type::host(className)});
    std::set<std::string> modules;
    for (const auto& r : document.world.instances) if (r.className == "ModuleScript") modules.insert(r.name);
    options.resolveModule = [modules](const std::string& name) -> std::optional<lim::ModuleInterface> {
        if (modules.contains(name)) return lim::ModuleInterface{name, {}, {}, {}};
        return std::nullopt;
    };
    editor.complete = [options](const std::string& source, std::size_t offset) {
        std::vector<std::string> result;
        for (const auto& item : lim::tooling::complete(source, static_cast<std::uint32_t>(offset), {ld::EngineTypes::shared().registry(), options}))
            result.push_back(item.insert.empty() ? item.label : item.insert);
        return result;
    };
    editor.inspect = [options](const std::string& source, std::size_t offset) {
        auto info = lim::tooling::hover(source, static_cast<std::uint32_t>(offset), {ld::EngineTypes::shared().registry(), options});
        return info ? info->title + (info->documentation.empty() ? "" : "\n" + info->documentation) : std::string();
    };
    editor.commands.push_back({tr("Formatta script"), "", true, [](const std::string& source, std::size_t, const std::string&) {
        ui::EditorReply reply;
        auto formatted = lim::tooling::format(source);
        if (!formatted.diagnostics.empty()) reply.message = formatted.diagnostics.front().message;
        else if (formatted.changed) reply.text = formatted.text;
        return reply;
    }});
    editor.commands.push_back({tr("Vai alla definizione"), "", false, [options](const std::string& source, std::size_t offset, const std::string&) {
        ui::EditorReply reply;
        auto range = lim::tooling::definition(source, static_cast<std::uint32_t>(offset), {ld::EngineTypes::shared().registry(), options});
        if (range) reply.selection = {range->start.offset, range->end.offset - range->start.offset};
        else reply.message = tr("Nessuna definizione nello script corrente.");
        return reply;
    }});
    editor.commands.push_back({tr("Trova riferimenti"), "", false, [options](const std::string& source, std::size_t offset, const std::string&) {
        ui::EditorReply reply;
        auto ranges = lim::tooling::references(source, static_cast<std::uint32_t>(offset), {ld::EngineTypes::shared().registry(), options});
        if (ranges.empty()) reply.message = tr("Nessun riferimento nello script corrente.");
        else {
            reply.message = tr("Riferimenti nello script corrente:");
            for (const auto& range : ranges) reply.message += "\n" + tr("Riga {0}, colonna {1}", range.start.line, range.start.column);
        }
        return reply;
    }});
    editor.commands.push_back({tr("Rinomina simbolo…"), tr("Nuovo nome"), true, [options](const std::string& source, std::size_t offset, const std::string& name) {
        ui::EditorReply reply;
        auto renamed = lim::tooling::rename(source, static_cast<std::uint32_t>(offset), name, {ld::EngineTypes::shared().registry(), options});
        if (!renamed.error.empty()) reply.message = renamed.error;
        else reply.text = lim::tooling::applyEdits(source, std::move(renamed.edits));
        return reply;
    }});
    editor.commands.push_back({tr("Attiva o disattiva il punto di interruzione"), "", false, [this, id](const std::string& source, std::size_t offset, const std::string&) {
        ui::EditorReply reply;
        if (document.playing) reply.message = tr("Ferma la prova per modificare i punti di interruzione.");
        else {
            auto line = 1 + std::count(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(std::min(offset, source.size())), '\n');
            document.toggleBreakpoint(id, static_cast<std::uint32_t>(line));
        }
        return reply;
    }});
    editor.editBoundary = [this] { document.endGesture(); };
    editor.historyAction = [this](bool redo) {
        document.run(redo ? ld::StudioCommand::redo : ld::StudioCommand::undo);
    };
    editor.historyEnabled = [this](bool redo) {
        return document.can(redo ? ld::StudioCommand::redo : ld::StudioCommand::undo);
    };
    editor.changed = [this, id](const std::string& value) { document.setSource(id, value); };
    if (document.focusLine) document.focusLine.reset();
    finder = FindAction::none;
    std::vector<NodePtr> pane{hstack(std::move(header), spacing::s4)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::panelHeader + spacing::s2)->background(rectangle(colors::panelHeader)), ui::divider(), ui::textField(editor)->fill()};
    if (document.debugPaused()) {
        const auto& stopped = document.debugStops.begin()->second;
        std::vector<NodePtr> stack;
        for (const auto& frame : stopped.frames) {
            stack.push_back(text(frame.function + " · " + frame.file + ":" + std::to_string(frame.line), type::smallStrong));
            for (const auto& local : frame.locals) stack.push_back(text(local.name + " = " + local.value, type::caption));
        }
        auto controls = hstack({text(tr("Debugger · {0}", ld::contextTitle(document.debugStops.begin()->first)), type::sectionTitle), spacer(),
            button("studio.debug.continue", tr("Continua@@debug"), [this] { document.resumeDebug(lim::StepMode::run); })->help(tr("Riprendi l’esecuzione")),
            button("studio.debug.into", tr("Entra@@debug"), [this] { document.resumeDebug(lim::StepMode::into); })->help(tr("Entra nella funzione chiamata")),
            button("studio.debug.over", tr("Passo@@debug"), [this] { document.resumeDebug(lim::StepMode::over); })->help(tr("Esegui la riga corrente")),
            button("studio.debug.out", tr("Esci@@debug"), [this] { document.resumeDebug(lim::StepMode::out); })->help(tr("Esci dalla funzione corrente"))}, spacing::s4);
        pane.push_back(ui::divider());
        pane.push_back(vstack({controls, scroll("studio.debug.stack", vstack(std::move(stack), spacing::s2, HAlign::leading))}, spacing::s3, HAlign::leading)->padding(spacing::s4)->frameSize(std::nullopt, 160)->background(rectangle(colors::surfaceAlt)));
    }
    return vstack(std::move(pane), 0)->background(rectangle(colors::surface));
}

NodePtr StudioShell::category(const std::string& title) {
    stripe = 0;
    return embossed(text(title, type::smallStrong, colors::textSecondary), emboss::light)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::rowHeight)->fillWidth(Alignment::leading())->background(sheet(sheen::category));
}

NodePtr StudioShell::propertyCell(NodePtr name, NodePtr content) {
    auto line = hstack({name->frameSize(metrics::propertyName - spacing::s4, std::nullopt, Alignment::leading())->paddingHorizontal(spacing::s4 / 2), rectangle(colors::separator)->frameSize(borders::pixels, std::nullopt), content->paddingHorizontal(spacing::s2)->fillWidth(Alignment::leading())}, 0)
                    ->padding(Insets{0, spacing::s2, 0, 0})
                    ->frameSize(std::nullopt, metrics::rowHeight + spacing::s1)
                    ->fillWidth(Alignment::leading());
    return line->background(rectangle(stripe++ % 2 ? colors::rowAlternate : colors::surface));
}

NodePtr StudioShell::row(const std::string& title, NodePtr content, const std::string& help) {
    auto label = lineLimited(text(title, type::small, colors::textPrimary), 1);
    return propertyCell(help.empty() ? label : label->help(title + "\n" + help), std::move(content));
}

NodePtr StudioShell::readOnly(const std::string& value) { return lineLimited(text(value, type::small, colors::textSecondary), 1)->fillWidth(Alignment::leading()); }

NodePtr StudioShell::toggleRow(const std::string& id, const std::string& title, bool value, std::function<void(bool)> change, const std::string& help) {
    return row(title, toggle(id, value, [this, change](bool next) {
                          change(next);
                          refresh();
                      }), help);
}

NodePtr StudioShell::numberField(const std::string& id, double value, double low, double high, double step, int digits, std::function<void(double)> change) {
    std::string shown = editing.count(id) ? drafts[id] : ld::localization::decimal(value, digits);
    auto commit = [this, id, low, high, digits, change] {
        editing.erase(id);
        auto parsed = parseNumber(drafts[id]);
        if (!parsed) {
            invalidate();
            return;
        }
        double updated = std::min(high, std::max(low, digits == 0 ? std::round(*parsed) : *parsed));
        change(updated);
        refresh();
    };
    NativeField field;
    field.id = id;
    field.text = shown;
    field.font = type::small;
    field.color = colors::textPrimary;
    field.changed = [this, id](const std::string& next) {
        drafts[id] = next;
        editing.insert(id);
    };
    field.commit = [commit](const std::string&) { commit(); };
    field.focus = [this, id, commit](bool focused) {
        if (!focused && editing.count(id)) commit();
    };
    auto input = ui::textField(field)->paddingHorizontal(spacing::s3)->flexible(std::nullopt, infinity, metrics::compactControlHeight, metrics::compactControlHeight);
    return classicBorder(input->background(rectangle(colors::control))->clipped(metrics::controlRadius), metrics::controlRadius);
}

NodePtr StudioShell::vectorField(const std::string& id, const std::vector<double>& values, const std::vector<std::pair<double, double>>& ranges, std::function<void(int, double)> change) {
    std::string formatted;
    for (std::size_t i = 0; i < values.size(); ++i) formatted += (i ? vectorSeparator() : "") + ld::localization::decimal(values[i], 2);
    NativeField field;
    field.id = id;
    field.text = editing.count(id) ? drafts[id] : formatted;
    field.font = type::small;
    field.color = colors::textPrimary;
    field.changed = [this, id](const std::string& next) {
        drafts[id] = next;
        editing.insert(id);
    };
    field.commit = [this, id, values, ranges, change](const std::string&) {
        editing.erase(id);
        std::vector<std::optional<double>> parts;
        for (const auto& piece : vectorParts(drafts[id])) parts.push_back(parseNumber(piece));
        if (parts.size() != values.size() || std::any_of(parts.begin(), parts.end(), [](const auto& p) { return !p; })) {
            invalidate();
            return;
        }
        for (std::size_t i = 0; i < values.size(); ++i) {
            double value = std::min(ranges[i].second, std::max(ranges[i].first, *parts[i]));
            if (value != values[i]) change(static_cast<int>(i), value);
        }
        refresh();
    };
    return classicBorder(ui::textField(field)->paddingHorizontal(spacing::s3)->flexible(std::nullopt, infinity, metrics::compactControlHeight, metrics::compactControlHeight)->background(rectangle(colors::control))->clipped(metrics::controlRadius), metrics::controlRadius);
}

NodePtr StudioShell::colorField(const std::string& id, const std::string& hex, std::function<void(const std::string&)> change) {
    NativeField field;
    field.id = id;
    field.text = editing.count(id) ? drafts[id] : components(hex);
    field.font = type::small;
    field.color = colors::textPrimary;
    field.changed = [this, id](const std::string& next) {
        drafts[id] = next;
        editing.insert(id);
    };
    field.commit = [this, id, change](const std::string&) {
        editing.erase(id);
        if (auto value = parseComponents(drafts[id])) change(*value);
        refresh();
    };
    return classicBorder(ui::textField(field)->paddingHorizontal(spacing::compact)->flexible(std::nullopt, infinity, metrics::compactControlHeight, metrics::compactControlHeight)->background(rectangle(colors::control))->clipped(metrics::controlRadius), metrics::controlRadius)->id("properties.color");
}

NodePtr StudioShell::textField(const std::string& id, const std::string& value, std::function<void(const std::string&)> commit) {
    NativeField field;
    field.id = id;
    field.text = editing.count(id) ? drafts[id] : value;
    field.font = type::small;
    field.changed = [this, id](const std::string& next) {
        drafts[id] = next;
        editing.insert(id);
    };
    field.commit = [this, id, value, commit](const std::string&) {
        editing.erase(id);
        if (drafts[id] != value) commit(drafts[id]);
        refresh();
    };
    return styledField(field);
}

NodePtr StudioShell::classicField(const std::string& id, const std::string& prompt, const std::string& value, std::function<void(const std::string&)> change, bool enabled) {
    NativeField field;
    field.id = id;
    field.prompt = prompt;
    field.text = value;
    field.editable = enabled;
    field.changed = [this, change](const std::string& next) {
        change(next);
        invalidate();
    };
    auto node = styledField(field);
    return enabled ? node : node->disabled();
}

NodePtr StudioShell::axis(const ld::WorldBlock& block, const std::string& title, double x, double y, double z, double low, double high, double step, std::function<void(int, double)> change) {
    std::pair<double, double> range{low, high};
    std::pair<double, double> vertical = title == "Position" ? std::make_pair(-ld::studio_limits::vertical, ld::studio_limits::vertical) : range;
    bool open = document.openVectors.count(title) > 0;
    auto toggleOpen = plainButton("vector.toggle." + title, ui::icon(open ? "ld-icon-studio-disclosure-open" : "ld-icon-studio-disclosure-closed", 11)->frameSize(11, std::nullopt), [this, title, open] {
        if (open) document.openVectors.erase(title);
        else document.openVectors.insert(title);
        invalidate();
    });
    std::vector<NodePtr> rows{propertyCell(hstack({toggleOpen, lineLimited(text(propertyLabel(title), type::small), 1)}, spacing::s1)->help(propertyLabel(title) + "\n" + scriptName(block.className(), title)), vectorField("vector." + block.id + "." + title, {x, y, z}, {range, vertical, range}, change))->id("properties.vector." + title)};
    if (open) {
        rows.push_back(row("   X", numberField("number." + block.id + "." + title + ".x", x, range.first, range.second, step, 2, [change](double v) { change(0, v); })));
        rows.push_back(row("   Y", numberField("number." + block.id + "." + title + ".y", y, vertical.first, vertical.second, step, 2, [change](double v) { change(1, v); })));
        rows.push_back(row("   Z", numberField("number." + block.id + "." + title + ".z", z, range.first, range.second, step, 2, [change](double v) { change(2, v); })));
    }
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr StudioShell::palette(const std::string& key, const std::vector<std::string>& choices, const std::string& current, std::function<void(const std::string&)> pick) {
    std::string title = propertyLabel(key);
    std::vector<NodePtr> swatches;
    for (const auto& c : choices) {
        std::vector<NodePtr> layers{rectangle(Color::hex(c))};
        if (current == c) layers.push_back(glyph("spunta", iconSize::compact, markOn(Color::hex(c))));
        swatches.push_back(plainButton("palette." + key + "." + c, classicBorder(zstack(std::move(layers))->frameSize(metrics::swatch + spacing::s2, metrics::swatch + spacing::s2)->clipped(metrics::smallRadius), metrics::smallRadius), [this, pick, c] {
                               pick(c);
                               refresh();
                           })->help(title + " " + c));
    }
    double cell = metrics::swatch + spacing::s2;
    return vstack({text(title, type::small)->help(title + "\n" + scriptName("Lighting", key)), grid(std::move(swatches), cell, cell, spacing::s2, spacing::s2)}, spacing::s2, HAlign::leading)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s3);
}

NodePtr StudioShell::waterSwatches(const std::vector<std::string>& choices, const std::string& current, std::function<void(const std::string&)> pick) {
    std::vector<NodePtr> swatches;
    for (const auto& c : choices) {
        bool chosen = ld::uppercase(current) == c;
        std::vector<NodePtr> layers{rectangle(Color::hex(c))};
        if (chosen) layers.push_back(glyph("spunta", iconSize::compact, markOn(Color::hex(c))));
        swatches.push_back(plainButton("terrain.water.swatch." + c, zstack(std::move(layers))->frameSize(metrics::swatch + spacing::s2, metrics::swatch + spacing::s2)->clipped(metrics::smallRadius)->overlay(strokeRectangle(chosen ? colors::accent : colors::border, chosen ? borders::focus : borders::pixels, metrics::smallRadius)), [pick, c] { pick(c); })->help(c));
    }
    return grid(std::move(swatches), metrics::swatch + spacing::s2, infinity, spacing::s2, spacing::s2)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s3);
}

NodePtr StudioShell::terrainForm() {
    const ld::TerrainWater water = document.world.terrain.waterProperties;
    auto update = [this](std::function<void(ld::TerrainWater&)> change) {
        ld::TerrainWater next = document.world.terrain.waterProperties;
        change(next);
        document.perform(ld::StudioEdit::waterProperties(next));
        refresh();
    };
    namespace limits = ld::rules::terrain::water;
    std::vector<std::string> swatches(limits::palette.begin(), limits::palette.end());
    return vstack({row(propertyLabel("Name"), readOnly("Terrain")), row(propertyLabel("ClassName"), readOnly("Terrain")), category(tr("Acqua")),
                   row(tr("Colore"), textField("terrain.water.color", water.color, [update](const std::string& value) {
                       std::string hex = ld::trim(value);
                       if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
                       if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return;
                       update([hex](ld::TerrainWater& w) { w.color = hex; });
                   })),
                   waterSwatches(swatches, water.color, [update](const std::string& hex) { update([hex](ld::TerrainWater& w) { w.color = hex; }); }),
                   row(tr("Trasparenza"), plainNumber("terrain.water.transparency", water.transparency, limits::transparency::minimum, limits::transparency::maximum, 2, [update](double v) { update([v](ld::TerrainWater& w) { w.transparency = v; }); })),
                   row(tr("Riflessi"), plainNumber("terrain.water.reflectance", water.reflectance, limits::reflectance::minimum, limits::reflectance::maximum, 2, [update](double v) { update([v](ld::TerrainWater& w) { w.reflectance = v; }); })),
                   row(tr("Dimensione onde"), plainNumber("terrain.water.waveSize", water.waveSize, limits::waveSize::minimum, limits::waveSize::maximum, 2, [update](double v) { update([v](ld::TerrainWater& w) { w.waveSize = v; }); })),
                   row(tr("Velocità onde"), plainNumber("terrain.water.waveSpeed", water.waveSpeed, limits::waveSpeed::minimum, limits::waveSpeed::maximum, 1, [update](double v) { update([v](ld::TerrainWater& w) { w.waveSpeed = v; }); }))},
                  0, HAlign::leading)
        ->id("terrain.form");
}

NodePtr StudioShell::lightingForm() {
    const auto& lighting = document.world.lighting;
    namespace L = ld::rules::lighting;
    auto named = [](const std::string& key) { return propertyLabel(key); };
    auto help = [](const std::string& key) { return scriptName("Lighting", key); };
    auto content = vstack({category(tr("Aspetto")),
                           row(named("ClockTime"), numberField("lighting.clock", lighting.clockTime, L::clockTime::minimum, L::clockTime::maximum, 0.5, 2, [this](double v) { document.setLighting("ClockTime", [v](ld::WorldLighting& l) { l.clockTime = v; }); }), help("ClockTime")),
                           row(named("Brightness"), numberField("lighting.brightness", lighting.brightness, L::brightness::minimum, L::brightness::maximum, 0.1, 2, [this](double v) { document.setLighting("Brightness", [v](ld::WorldLighting& l) { l.brightness = v; }); }), help("Brightness")),
                           row(named("FogEnd"), numberField("lighting.fog", lighting.fogEnd, L::fogEnd::minimum, L::fogEnd::maximum, 20, 2, [this](double v) { document.setLighting("FogEnd", [v](ld::WorldLighting& l) { l.fogEnd = v; }); }), help("FogEnd")),
                           palette("Ambient", ambientColors, lighting.ambient, [this](const std::string& c) { document.setLighting("Ambient", [c](ld::WorldLighting& l) { l.ambient = c; }); }), category(tr("Cielo")),
                           palette("SkyTop", skyColors, lighting.skyTop, [this](const std::string& c) { document.setLighting("SkyTop", [c](ld::WorldLighting& l) { l.skyTop = c; }); }),
                           palette("SkyHorizon", skyColors, lighting.skyHorizon, [this](const std::string& c) { document.setLighting("SkyHorizon", [c](ld::WorldLighting& l) { l.skyHorizon = c; }); })},
                          0, HAlign::leading)
                       ->padding(Insets{0, 0, spacing::panel, 0});
    return scroll("properties.lighting.scroll", content->fillWidth(Alignment::leading()))->id("properties.lighting");
}

NodePtr StudioShell::partForm(const ld::WorldBlock& block) {
    std::string id = block.id;
    auto update = [this, id](std::function<void(ld::WorldBlock&)> change, std::optional<std::string> label = std::nullopt, std::optional<std::string> coalesce = std::nullopt) {
        document.update(id, change, label, coalesce);
        refresh();
    };
    std::string kind = block.className();
    std::vector<NodePtr> rows{category(tr("Dati")),
                              row(propertyLabel("Name"), classicField("block.name." + id, propertyLabel("Name"), block.name, [update](const std::string& v) { update([v](ld::WorldBlock& b) { b.name = ld::utf8Prefix(v, 40); }, tr("Rinomina"), "name:" + std::string()); }), scriptName(kind, "Name")),
                              row(propertyLabel("ClassName"), readOnly(classText(kind)), scriptName(kind, "ClassName")),
                              row(propertyLabel("Parent"), readOnly(shownName(block.parent)), scriptName(kind, "Parent")), category(tr("Trasformazione"))};
    rows.push_back(axis(block, "Position", block.x, block.y, block.z, -ld::studio_limits::horizontal, ld::studio_limits::horizontal, ld::studio_limits::snap, [update](int i, double v) {
        update([i, v](ld::WorldBlock& b) {
            if (i == 0) b.x = v;
            else if (i == 1) b.y = v;
            else b.z = v;
        });
    }));
    rows.push_back(axis(block, "Orientation", block.rotationX, block.rotationY, block.rotationZ, -180, 180, document.angleSnap, [update](int i, double v) {
        update([i, v](ld::WorldBlock& b) {
            if (i == 0) b.rotationX = v;
            else if (i == 1) b.rotationY = v;
            else b.rotationZ = v;
        });
    }));
    rows.push_back(axis(block, "Size", block.sizeX, block.sizeY, block.sizeZ, ld::studio_limits::minimumSize, ld::studio_limits::maximumSize, ld::studio_limits::snap, [update](int i, double v) {
        update([i, v](ld::WorldBlock& b) {
            if (i == 0) b.sizeX = v;
            else if (i == 1) b.sizeY = v;
            else b.sizeZ = v;
        });
    }));
    rows.push_back(category(tr("Aspetto")));
    rows.push_back(row(propertyLabel("Color"), hstack({classicBorder(rectangle(Color::hex(block.color))->frameSize(metrics::swatch, metrics::swatch - spacing::s1), metrics::smallRadius), colorField("block.color." + id, block.color, [update](const std::string& v) { update([v](ld::WorldBlock& b) { b.color = v; }, tr("Colore")); })}, spacing::compact), scriptName(kind, "Color")));
    std::vector<NodePtr> swatches;
    for (auto entry : ld::rules::part::palette) {
        std::string c(entry);
        std::vector<NodePtr> layers{rectangle(Color::hex(c))};
        bool chosen = block.color == c;
        if (chosen) layers.push_back(glyph("spunta", iconSize::compact, markOn(Color::hex(c))));
        swatches.push_back(plainButton("swatch." + c, zstack(std::move(layers))->frameSize(metrics::swatch + spacing::s2, metrics::swatch + spacing::s2)->clipped(metrics::smallRadius)->overlay(strokeRectangle(chosen ? colors::accent : colors::border, chosen ? borders::focus : borders::pixels, metrics::smallRadius)), [update, c] { update([c](ld::WorldBlock& b) { b.color = c; }, tr("Colore")); })->help(tr("Colore {0}", c)));
    }
    double cell = metrics::swatch + spacing::s2;
    rows.push_back(grid(std::move(swatches), cell, cell, spacing::s2, spacing::s2)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s3));
    NativeField material;
    material.id = "block.material." + id;
    int selected = 0;
    for (std::size_t i = 0; i < ld::rules::materials.size(); ++i) {
        material.options.push_back(ld::studio_catalog::materialTitle(ld::rules::materials[i]));
        if (block.material == ld::rules::materials[i]) selected = static_cast<int>(i);
    }
    material.selectedOption = selected;
    material.editable = !document.playing;
    material.choose = [update](int i) { update([i](ld::WorldBlock& b) { b.material = std::string(ld::rules::materials[static_cast<std::size_t>(i)]); }, tr("Materiale")); };
    rows.push_back(row(propertyLabel("Material"), select(material)->fillWidth(), scriptName(kind, "Material")));
    rows.push_back(row(propertyLabel("Transparency"), numberField("block.transparency." + id, block.transparency, 0, 1, 0.05, 2, [update, id](double v) { update([v](ld::WorldBlock& b) { b.transparency = v; }, tr("Trasparenza"), "transparency:" + id); }), scriptName(kind, "Transparency")));
    rows.push_back(row(propertyLabel("Reflectance"), numberField("block.reflectance." + id, block.reflectance, 0, 1, 0.05, 2, [update, id](double v) { update([v](ld::WorldBlock& b) { b.reflectance = v; }, tr("Riflettenza"), "reflectance:" + id); }), scriptName(kind, "Reflectance")));
    rows.push_back(category(tr("Comportamento")));
    rows.push_back(toggleRow("block.anchored", propertyLabel("Anchored"), block.anchored, [update](bool v) { update([v](ld::WorldBlock& b) { b.anchored = v; }, tr("Ancorato")); }, tr("Se attivo, la fisica non sposta l’oggetto.") + "\n" + scriptName(kind, "Anchored")));
    rows.push_back(toggleRow("block.cancollide", propertyLabel("CanCollide"), block.canCollide, [update](bool v) { update([v](ld::WorldBlock& b) { b.canCollide = v; }, tr("Collisioni")); }, tr("Personaggi e oggetti ci sbattono contro.") + "\n" + scriptName(kind, "CanCollide")));
    rows.push_back(toggleRow("block.locked", propertyLabel("Locked"), block.locked, [update](bool v) { update([v](ld::WorldBlock& b) { b.locked = v; }, tr("Bloccato")); }, tr("Non si può selezionare cliccando nel mondo.") + "\n" + scriptName(kind, "Locked")));
    return scroll("properties.form", vstack(std::move(rows), 0, HAlign::leading)->padding(Insets{0, 0, spacing::panel, 0})->fillWidth(Alignment::leading()))->id("properties.scroll");
}

NodePtr StudioShell::propertyEditor(const ld::InstanceRecord& record, const std::string& name, const ld::PropertyDescriptor& info, const std::optional<ld::PropertyValue>& value) {
    std::string id = record.id;
    auto set = [this, id, name](const ld::PropertyValue& next) {
        document.setProperty(id, name, next);
        refresh();
    };
    using Kind = ld::PropertyValue::Kind;
    std::string key = "property." + id + "." + name;
    std::string label = propertyLabel(name), help = scriptName(record.className, name);
    if (info.category == "Primitive" && info.typeName == "bool") return toggleRow(key, label, value && value->kind == Kind::boolean && value->flag, [set](bool v) { set(ld::PropertyValue::boolean(v)); }, help);
    if (info.category == "Primitive" && info.typeName == "string")
        return row(label, textField(key, value && value->kind == Kind::string ? value->text : "", [set](const std::string& v) { set(ld::PropertyValue::string(ld::utf8Prefix(v, static_cast<std::size_t>(ld::rules::instances::textCharacters)))); }), help);
    if (info.category == "DataType" && info.typeName == "Content") {
        auto pick = [this, id, name] {
            if (!chooseContent) return;
            auto file = chooseContent(ld::studio_catalog::contentExtensions(name));
            if (!file) return;
            auto result = document.importContent(id, name, *file);
            if (!result.ok()) {
                store->error = result.error().message;
                document.log(result.error().message, true);
            }
            refresh();
        };
        return row(label, hstack({textField(key, value && value->kind == Kind::content ? value->text : "", [set](const std::string& v) { set(ld::PropertyValue::content(v)); })->fillWidth(), button("property.import." + id + "." + name, tr("Importa…"), pick)}, spacing::s2), help);
    }
    if (info.category == "Primitive") {
        bool integer = info.typeName == "int";
        return row(label, numberField(key, value && value->kind == Kind::number ? value->numbers[0] : 0, -1000000, 1000000, integer ? 1 : 0.1, integer ? 0 : 2, [set, integer](double v) { set(ld::PropertyValue::number(integer ? std::round(v) : v)); }), help);
    }
    if (info.category == "DataType" && (info.typeName == "Color3" || info.typeName == "BrickColor")) {
        std::string hex = value && value->kind == Kind::color3 ? value->text : "FFFFFF";
        return row(label, hstack({classicBorder(rectangle(Color::hex(hex))->frameSize(metrics::swatch, metrics::swatch - spacing::s1), metrics::smallRadius), colorField(key, hex, [set](const std::string& v) { set(ld::PropertyValue::color3(v)); })}, spacing::compact), help);
    }
    std::pair<double, double> wide{-10000, 10000};
    if (info.category == "DataType" && value) {
        auto n = value->numbers;
        if (info.typeName == "Vector3" && value->kind == Kind::vector3)
            return row(label, vectorField(key, {n[0], n[1], n[2]}, {wide, wide, wide}, [set, n](int i, double v) {
                           auto next = n;
                           next[static_cast<std::size_t>(i)] = v;
                           set(ld::PropertyValue::vector3(next[0], next[1], next[2]));
                       }), help);
        if (info.typeName == "Vector2" && value->kind == Kind::vector2)
            return row(label, vectorField(key, {n[0], n[1]}, {wide, wide}, [set, n](int i, double v) {
                           auto next = n;
                           next[static_cast<std::size_t>(i)] = v;
                           set(ld::PropertyValue::vector2(next[0], next[1]));
                       }), help);
        if (info.typeName == "UDim" && value->kind == Kind::udim)
            return row(label, vectorField(key, {n[0], n[1]}, {wide, wide}, [set, n](int i, double v) {
                           auto next = n;
                           next[static_cast<std::size_t>(i)] = v;
                           set(ld::PropertyValue::udim(next[0], next[1]));
                       }), help);
        if (info.typeName == "UDim2" && value->kind == Kind::udim2)
            return row(label, vectorField(key, {n[0], n[1], n[2], n[3]}, {wide, wide, wide, wide}, [set, n](int i, double v) {
                           auto next = n;
                           next[static_cast<std::size_t>(i)] = v;
                           set(ld::PropertyValue::udim2(next[0], next[1], next[2], next[3]));
                       }), help);
    }
    if (info.category == "Enum") {
        auto found = document.registry.enums().find(info.typeName);
        std::vector<std::string> items = found == document.registry.enums().end() ? std::vector<std::string>{} : found->second;
        std::string current = value && value->kind == Kind::enumItem ? value->text : items.empty() ? "" : items.front();
        NativeField picker;
        picker.id = key;
        picker.options = items;
        picker.editable = !document.playing;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i] == current) picker.selectedOption = static_cast<int>(i);
        picker.choose = [set, items](int i) { set(ld::PropertyValue::enumItem(items[static_cast<std::size_t>(i)])); };
        return row(label, select(picker)->fillWidth(), help);
    }
    if (info.category == "Class") {
        std::string target = value && value->referenceId() ? document.displayName(*value->referenceId()) : tr("Nessuno");
        return row(label, readOnly(target), help);
    }
    return row(label, readOnly(info.typeName), help);
}

NodePtr StudioShell::instanceForm(const ld::InstanceRecord& record) {
    auto descriptor = document.registry.find(record.className);
    auto values = document.registry.defaults(record.className);
    for (const auto& [k, v] : record.properties) values[k] = v;
    const std::string& kind = record.className;
    std::vector<NodePtr> rows{category(tr("Dati")), row(propertyLabel("Name"), textField("instance.name." + record.id, record.name, [this, id = record.id](const std::string& v) { document.rename(id, v); }), scriptName(kind, "Name")),
                              row(propertyLabel("ClassName"), readOnly(classText(kind)), scriptName(kind, "ClassName")),
                              row(propertyLabel("Parent"), readOnly(shownName(record.parent)), scriptName(kind, "Parent"))};
    std::vector<std::string> editable;
    if (descriptor)
        for (const auto& name : descriptor->supported)
            if (name != "Name" && name != "Source" && descriptor->properties.count(name)) editable.push_back(name);
    for (const auto& name : editable) {
        auto found = values.find(name);
        rows.push_back(propertyEditor(record, name, descriptor->properties.at(name), found == values.end() ? std::nullopt : std::optional<ld::PropertyValue>(found->second)));
    }
    if (document.isScript(record.id)) {
        rows.push_back(category(tr("Sorgente")));
        std::string source = record.source();
        std::size_t lines = static_cast<std::size_t>(std::count(source.begin(), source.end(), '\n')) + 1;
        rows.push_back(hstack({text(trn("{0} riga", "{0} righe", static_cast<long long>(lines)), type::small, colors::textSecondary), spacer(0.0), document.activeScript == record.id ? text(tr("Aperto nell’editor"), type::caption, colors::textTertiary) : button("instance.openScript", tr("Apri script"), [this, id = record.id] { document.openScript(id); })}, spacing::inlineGap)->paddingHorizontal(spacing::panel)->paddingVertical(spacing::compact));
    }
    return scroll("properties.instance", vstack(std::move(rows), 0, HAlign::leading)->padding(Insets{0, 0, spacing::panel, 0})->fillWidth(Alignment::leading()))->id("properties.instance." + record.className);
}

NodePtr StudioShell::properties() {
    const auto& selection = document.selection();
    auto part = document.selectedPart();
    std::vector<NodePtr> column;
    std::optional<std::string> detail;
    if (document.playing) detail = tr("sola lettura durante la prova");
    else if (document.selectedParts().size() > 1) detail = trn("{0} selezionato", "{0} selezionati", static_cast<long long>(document.selectedParts().size()));
    else if (selection) detail = shownName(selection->id);
    column.push_back(panelHeader(tr("Proprietà"), detail, "properties"));
    if (selection && selection->service && selection->id == "Lighting") column.push_back(lightingForm()->disabled(document.playing));
    else if (selection && selection->service && selection->id == "Terrain") column.push_back(terrainForm()->disabled(document.playing));
    else if (selection && selection->service) {
        const std::string& service = selection->id;
        column.push_back(vstack({row(propertyLabel("Name"), readOnly(serviceTitle(service))), row(propertyLabel("ClassName"), readOnly(document.className(service).value_or(service))),
                                 row(tr("Contenuto"), readOnly(trn("{0} oggetto", "{0} oggetti", static_cast<long long>(document.children(service).size())))),
                                 text(serviceHelp(service), type::caption, colors::textSecondary)->fixed(false, true)->padding(spacing::s4)},
                                0, HAlign::leading));
    } else if (part && part->kind == "model") {
        std::string id = part->id;
        column.push_back(vstack({row(propertyLabel("Name"), classicField("model.name." + id, propertyLabel("Name"), part->name, [this, id](const std::string& v) {
                                          document.update(id, [v](ld::WorldBlock& b) { b.name = ld::utf8Prefix(v, static_cast<std::size_t>(ld::rules::world::nameCharacters)); }, tr("Rinomina"), "name:" + id, ld::StudioAction::rename);
                                          refresh();
                                      })),
                                 row(propertyLabel("ClassName"), readOnly(classText(part->className()))), row(propertyLabel("Parent"), readOnly(shownName(part->parent))),
                                 row(tr("Contenuto"), readOnly(trn("{0} oggetto", "{0} oggetti", static_cast<long long>(document.world.childIds(id).size())))),
                                 toggleRow("model.locked", propertyLabel("Locked"), part->locked, [this, id](bool v) { document.update(id, [v](ld::WorldBlock& b) { b.locked = v; }, tr("Bloccato"), std::nullopt, ld::StudioAction::unlock); }, tr("Non si può selezionare cliccando nel mondo."))},
                                0, HAlign::leading)
                             ->disabled(document.playing));
    } else if (part) column.push_back(partForm(*part)->disabled(document.playing));
    else if (auto record = document.selectedRecord()) column.push_back(instanceForm(*record)->disabled(document.playing));
    else column.push_back(vstack({text(tr("Nessun oggetto selezionato"), type::smallStrong, colors::textPrimary), text(tr("Seleziona un oggetto per vedere le sue proprietà."), type::caption, colors::textSecondary)->fixed(false, true)}, spacing::s2, HAlign::leading)->padding(spacing::panel)->fillWidth(Alignment::leading()));
    column.push_back(spacer(0.0));
    return vstack(std::move(column), 0)->background(rectangle(colors::surface));
}

NodePtr StudioShell::searchField(const std::string& id, const std::string& prompt, const std::string& value, std::function<void(const std::string&)> change) {
    NativeField field;
    field.id = id;
    field.prompt = prompt;
    field.text = value;
    field.font = type::small;
    field.color = colors::textPrimary;
    field.changed = [this, change](const std::string& next) {
        change(next);
        invalidate();
    };
    std::vector<NodePtr> items{ui::textField(field)};
    if (!value.empty()) items.push_back(plainButton(id + ".clear", glyph("chiudi", iconSize::compact, colors::textSecondary), [this, change] {
                                            change("");
                                            invalidate();
                                        })->help(tr("Cancella ricerca")));
    return hstack(std::move(items), spacing::s3)->paddingHorizontal(spacing::s3)->frameSize(std::nullopt, metrics::compactControlHeight)->background(rectangle(colors::control))->clipped(metrics::controlRadius)->overlay(strokeRectangle(colors::border, borders::pixels, metrics::controlRadius))->id("utility.searchField");
}

NodePtr StudioShell::toolboxTile(const std::string& id, const std::string& glyph, const std::string& title, bool enabled, std::function<void()> action, const std::string& help) {
    Surface* surface = Surface::active();
    std::string key = "toolbox.tile." + title;
    bool hovered = enabled && surface && surface->hovered(key), pressed = enabled && surface && surface->pressed(key);
    auto content = vstack({icon(glyph, iconSize::ribbon)->opacity(enabled ? 1 : tokens::opacity::disabled), lineLimited(text(title, type::smallStrong, enabled ? colors::textPrimary : colors::textDisabled), 1)}, spacing::s3)
                       ->paddingVertical(spacing::s4)
                       ->frameSize(metrics::tile + spacing::s4, std::nullopt)
                       ->background(rectangle(pressed ? colors::rowAlternate : colors::surface))
                       ->clipped(metrics::frameRadius)
                       ->overlay(strokeRectangle(hovered || pressed ? colors::accent : colors::border, borders::pixels, metrics::frameRadius))
                       ->boxShadow(drops::group.color, 0, drops::group.dy, 0, metrics::frameRadius);
    auto result = plainButton(key, content, [this, action] {
        action();
        refresh();
    }, Cursor::pointer);
    if (!help.empty()) result = result->help(help);
    return enabled ? result : result->disabled();
}

struct TerrainChoice {
    std::string key, icon;
    std::optional<ld::TerrainTool> tool;
};

const std::vector<TerrainChoice>& terrainChoices() {
    static const std::vector<TerrainChoice> list{{"generate", "ld-icon-terrain-generate", std::nullopt},
                                                 {"add", "ld-icon-terrain-add", ld::TerrainTool::add},
                                                 {"subtract", "ld-icon-terrain-subtract", ld::TerrainTool::subtract},
                                                 {"raise", "ld-icon-terrain-raise", ld::TerrainTool::raise},
                                                 {"smooth", "ld-icon-terrain-smooth", ld::TerrainTool::smooth},
                                                 {"flatten", "ld-icon-terrain-flatten", ld::TerrainTool::flatten},
                                                 {"paint", "ld-icon-terrain-paint", ld::TerrainTool::paint},
                                                 {"sea", "ld-icon-terrain-sea", std::nullopt}};
    return list;
}

std::string StudioShell::terrainHelp(ld::TerrainTool tool) const {
    switch (tool) {
    case ld::TerrainTool::add: return tr("Aggiungi: crea terreno dove passi il pennello.");
    case ld::TerrainTool::subtract: return tr("Sottrai: scava terreno e acqua.");
    case ld::TerrainTool::raise: return tr("Rigonfia: alza il terreno dolcemente.");
    case ld::TerrainTool::paint: return tr("Dipingi: cambia il materiale senza toccare la forma.");
    case ld::TerrainTool::smooth: return tr("Leviga: ammorbidisce spigoli e gradini.");
    case ld::TerrainTool::flatten: return tr("Appiana: livella il terreno all’altezza dove clicchi.");
    case ld::TerrainTool::water: return tr("Aggiungi: crea terreno dove passi il pennello.");
    }
    return "";
}

NodePtr StudioShell::plainNumber(const std::string& id, double value, double low, double high, int digits, std::function<void(double)> change) {
    std::string shown = editing.count(id) ? drafts[id] : ld::localization::decimal(value, digits);
    auto commit = [this, id, low, high, digits, change] {
        editing.erase(id);
        auto parsed = parseNumber(drafts[id]);
        if (!parsed) {
            invalidate();
            return;
        }
        change(std::clamp(digits == 0 ? std::round(*parsed) : *parsed, low, high));
        refresh();
    };
    NativeField field;
    field.id = id;
    field.text = shown;
    field.font = type::small;
    field.color = colors::textPrimary;
    field.changed = [this, id](const std::string& next) {
        drafts[id] = next;
        editing.insert(id);
    };
    field.commit = [commit](const std::string&) { commit(); };
    return classicBorder(ui::textField(field)->paddingHorizontal(spacing::s3)->flexible(std::nullopt, infinity, metrics::compactControlHeight, metrics::compactControlHeight)->background(rectangle(colors::control))->clipped(metrics::controlRadius), metrics::controlRadius);
}

NodePtr StudioShell::terrainPanel() {
    std::vector<std::string> keys{"create", "region"};
    auto tabs = segments("terrain.tab", {tr("Crea"), tr("Regione")}, document.terrainTab == "region" ? 1 : 0, [this, keys](int i) {
                    document.terrainTab = keys[static_cast<std::size_t>(i)];
                    document.closeTerrainTool();
                    refresh();
                }, true, keys);
    NodePtr body = document.terrainTab == "region" ? terrainRegionTab() : terrainCreateTab();
    return vstack({hstack({spacer(0.0), tabs, spacer(0.0)}, 0)->paddingVertical(spacing::s3)->fillWidth()->overlay(rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels), Alignment::bottom()), scroll("terrain.panel", body->padding(spacing::s4)->fillWidth(Alignment::topLeading()))}, 0)->background(rectangle(colors::surface))->id("terrain.settings");
}

NodePtr StudioShell::terrainCreateTab() {
    Surface* surface = Surface::active();
    std::vector<NodePtr> buttons;
    for (const auto& choice : terrainChoices()) {
        bool on = choice.tool ? document.terrainTool == choice.tool : document.terrainMode == std::optional<std::string>(choice.key);
        std::string id = "terrain.tool." + choice.key;
        bool hovered = surface && surface->hovered(id);
        std::string title = choice.key == "generate" ? tr("Genera") : choice.key == "sea" ? tr("Livello del mare") : ld::toolTitle(*choice.tool);
        auto face = vstack({ui::icon(choice.icon, iconSize::ribbon), lineLimited(text(title, type::micro, on ? colors::accent : colors::textPrimary)->multilineAlignment(TextAlign::center), 2)}, 0)
                        ->frameSize(metrics::terrainToolWidth, metrics::terrainToolHeight)
                        ->background(rectangle(on ? colors::selection : hovered ? colors::hover : colors::surface))
                        ->overlay(strokeRectangle(on ? colors::accent : colors::borderSubtle, borders::pixels, metrics::smallRadius))
                        ->clipped(metrics::smallRadius);
        buttons.push_back(plainButton(id, face, [this, choice, on] {
                              if (choice.tool) {
                                  document.terrainMode.reset();
                                  document.regionEditing = false;
                                  document.terrainTool = on ? std::nullopt : choice.tool;
                                  if (document.terrainTool == ld::TerrainTool::paint && document.terrainBrush.material == ld::TerrainMaterial::water) document.terrainBrush.material = ld::TerrainMaterial::grass;
                              } else {
                                  document.terrainTool.reset();
                                  document.terrainMode = on ? std::nullopt : std::optional<std::string>(choice.key);
                                  if (choice.key == "sea" && !document.regionCustom) document.terrainRegion = ld::TerrainRegion::whole(document.terrainSettings.waterLevel);
                              }
                              refresh();
                          })->help(choice.tool ? terrainHelp(*choice.tool) : title));
    }
    std::vector<NodePtr> rows{grid(std::move(buttons), metrics::terrainToolWidth, metrics::terrainToolWidth * 3 + spacing::s2 * 2, spacing::s2, spacing::s2)};
    rows.push_back(ui::divider());
    if (document.terrainTool) rows.push_back(terrainBrushSettings());
    else if (document.terrainMode == std::optional<std::string>("generate")) rows.push_back(terrainGenerateSettings());
    else if (document.terrainMode == std::optional<std::string>("sea")) rows.push_back(terrainSeaSettings());
    else rows.push_back(text(tr("Scegli uno strumento."), type::caption, colors::textSecondary));
    return vstack(std::move(rows), spacing::s4, HAlign::leading)->fillWidth(Alignment::topLeading());
}

NodePtr StudioShell::terrainField(const std::string& title, NodePtr content) {
    return hstack({text(title, type::small, colors::textSecondary)->frameSize(metrics::propertyName - spacing::s8, std::nullopt, Alignment::leading()), content->fillWidth(Alignment::leading())}, spacing::s2);
}

NodePtr StudioShell::terrainMaterials(bool allowWater, ld::TerrainMaterial current, std::function<void(ld::TerrainMaterial)> pick) {
    std::vector<NodePtr> swatches;
    std::vector<ld::TerrainMaterial> list(terrainSolids.begin(), terrainSolids.end());
    list.push_back(ld::TerrainMaterial::water);
    for (auto material : list) {
        bool chosen = current == material, enabled = allowWater || material != ld::TerrainMaterial::water;
        Color fill = Color::hex(std::string(ld::materialColor(material)));
        std::vector<NodePtr> layers{rectangle(fill)};
        if (chosen) layers.push_back(glyph("spunta", iconSize::compact, markOn(fill)));
        auto well = zstack(std::move(layers))->frameSize(metrics::materialSwatch, metrics::materialSwatch)->clipped(metrics::smallRadius)->overlay(strokeRectangle(chosen ? colors::accent : colors::border, chosen ? borders::focus : borders::pixels, metrics::smallRadius));
        auto face = vstack({well, text(ld::materialTitle(material), type::caption, chosen ? colors::textPrimary : colors::textSecondary)}, spacing::s1)->opacity(enabled ? 1 : 0.4);
        auto button = plainButton("terrain.material." + std::string(ld::materialName(material)), face, [this, material, pick] {
                          pick(material);
                          invalidate();
                      })->help(ld::materialTitle(material));
        swatches.push_back(enabled ? button : button->disabled());
    }
    return grid(std::move(swatches), metrics::materialSwatch + spacing::s6, infinity, spacing::s2, spacing::s3);
}

NodePtr StudioShell::terrainBrushSettings() {
    auto tool = *document.terrainTool;
    auto note = [](const std::string& value) { return text(value, type::caption, colors::textTertiary)->fixed(false, true); };
    bool surfacePlacement = document.terrainBrush.placement == ld::TerrainPlacement::surface;
    std::vector<NodePtr> rows{note(terrainHelp(tool)),
                              terrainField(tr("Dimensione"), hstack({slider("terrain.radius.slider", document.terrainBrush.radius, ld::rules::terrain::brush::minimum, ld::rules::terrain::brush::maximum, 1, colors::accent, [this](double v) { document.terrainBrush.radius = v; invalidate(); }, colors::border)->fill(),
                                                                   plainNumber("terrain.radius", document.terrainBrush.radius, ld::rules::terrain::brush::minimum, ld::rules::terrain::brush::maximum, 0, [this](double v) { document.terrainBrush.radius = v; })->frameSize(metrics::tile, std::nullopt)},
                                                                  spacing::s2)),
                              terrainField(tr("Forza"), hstack({slider("terrain.strength.slider", document.terrainBrush.strength, 0.05, 1, 0.05, colors::accent, [this](double v) { document.terrainBrush.strength = v; invalidate(); }, colors::border)->fill(),
                                                              plainNumber("terrain.strength", document.terrainBrush.strength, 0.05, 1, 2, [this](double v) { document.terrainBrush.strength = v; })->frameSize(metrics::tile, std::nullopt)},
                                                             spacing::s2)),
                              terrainField(tr("Posizionamento"), vstack({radio("terrain.placement", {tr("Sulla superficie"), tr("Nel punto")}, surfacePlacement ? 0 : 1, [this](int i) {
                                                                             document.terrainBrush.placement = i == 0 ? ld::TerrainPlacement::surface : ld::TerrainPlacement::free;
                                                                             refresh();
                                                                         }),
                                                                         note(surfacePlacement ? tr("Lavora in piano, sulla griglia") : tr("Lavora a sfera dove clicchi"))},
                                                                        spacing::s2, HAlign::leading)),
                              terrainField(tr("Materiale"), terrainMaterials(tool != ld::TerrainTool::paint, document.terrainBrush.material, [this](ld::TerrainMaterial m) { document.terrainBrush.material = m; }))};
    return vstack(std::move(rows), spacing::s4, HAlign::leading)->fillWidth(Alignment::leading())->id("terrain.brush");
}

NodePtr StudioShell::terrainGenerateSettings() {
    double top = ld::Terrain::origin.y + ld::Terrain::cellsY * ld::Terrain::cellSize;
    std::vector<NodePtr> rows{terrainField(tr("Seme"), plainNumber("terrain.seed", document.terrainSettings.seed, 1, 999999, 0, [this](double v) { document.terrainSettings.seed = static_cast<int>(v); })),
                              terrainField(tr("Ampiezza"), hstack({slider("terrain.amplitude.slider", document.terrainSettings.amplitude, 0, 12, 0.5, colors::accent, [this](double v) { document.terrainSettings.amplitude = v; invalidate(); }, colors::border)->fill(),
                                                                  plainNumber("terrain.amplitude", document.terrainSettings.amplitude, 0, 12, 1, [this](double v) { document.terrainSettings.amplitude = v; })->frameSize(metrics::tile, std::nullopt)},
                                                                 spacing::s2)),
                              terrainField(tr("Livello dell’acqua"), plainNumber("terrain.water", document.terrainSettings.waterLevel, ld::Terrain::origin.y, top, 2, [this](double v) { document.terrainSettings.waterLevel = std::round(v * 4) / 4; })),
                              hstack({button("terrain.generate", tr("Genera"), [this] {
                                          document.perform(ld::StudioEdit::generate(document.terrainSettings));
                                          if (!document.regionCustom) document.terrainRegion = ld::TerrainRegion::whole(document.terrainSettings.waterLevel);
                                      }, ButtonRole::primary),
                                      button("terrain.clear", tr("Cancella tutto"), [this] {
                                          confirmClearTerrain = true;
                                          invalidate();
                                      }, ButtonRole::destructive)->disabled(document.world.terrain.empty())},
                                     spacing::s2)};
    return vstack(std::move(rows), spacing::s4, HAlign::leading)->fillWidth(Alignment::leading())->id("terrain.generation");
}

NodePtr StudioShell::terrainRegionFields() {
    auto& r = document.terrainRegion;
    ld::Vec3 centre = (r.minimum + r.maximum) * 0.5, size = r.maximum - r.minimum;
    double span = ld::Terrain::cellsX * ld::Terrain::cellSize;
    auto set = [this](ld::Vec3 c, ld::Vec3 z) {
        document.terrainRegion = ld::TerrainRegion::snapped(c - z * 0.5, c + z * 0.5);
        document.regionCustom = true;
    };
    auto component = [&](const std::string& label, const std::string& id, double value, double low, double high, std::function<void(double)> change) {
        return hstack({text(label, type::caption, colors::textSecondary)->fixed(true, true), plainNumber(id, value, low, high, 2, std::move(change))}, spacing::s1);
    };
    auto axes = [&](const std::string& key, ld::Vec3 value, bool dimensions) {
        double low = dimensions ? 0.25 : -span, high = span;
        return hstack({component(dimensions ? tr("L@@larghezza") : "X", "terrain.region." + key + ".x", value.x, low, high, [=](double v) { ld::Vec3 c = centre, z = size; (dimensions ? z : c).x = v; set(c, z); }),
                       component(dimensions ? tr("A@@altezza") : "Y", "terrain.region." + key + ".y", value.y, low, high, [=](double v) { ld::Vec3 c = centre, z = size; (dimensions ? z : c).y = v; set(c, z); }),
                       component(dimensions ? tr("P@@profondità") : "Z", "terrain.region." + key + ".z", value.z, low, high, [=](double v) { ld::Vec3 c = centre, z = size; (dimensions ? z : c).z = v; set(c, z); })},
                      spacing::s2);
    };
    auto titled = [](const std::string& title, NodePtr content) { return vstack({text(title, type::small, colors::textSecondary), content->fillWidth(Alignment::leading())}, spacing::s1, HAlign::leading); };
    return vstack({titled(tr("Posizione"), axes("position", centre, false)), titled(tr("Dimensioni"), axes("size", size, true))}, spacing::s3, HAlign::leading)->fillWidth(Alignment::leading())->id("terrain.region.fields");
}

NodePtr StudioShell::terrainSeaSettings() {
    auto note = [](const std::string& value) { return text(value, type::caption, colors::textTertiary)->fixed(false, true); };
    return vstack({note(tr("Trascina le maniglie per scegliere la zona. L’acqua arriva fino al bordo superiore.")), terrainRegionFields(),
                   hstack({button("terrain.water.create", tr("Crea"), [this] { document.perform(ld::StudioEdit::sea(document.terrainRegion)); }, ButtonRole::primary),
                           button("terrain.water.remove", tr("Svuota"), [this] { document.perform(ld::StudioEdit::sea(document.terrainRegion, true)); })},
                          spacing::s2),
                   note(tr("Svuota toglie tutta l’acqua nella zona."))},
                  spacing::s4, HAlign::leading)
        ->fillWidth(Alignment::leading())
        ->id("terrain.sea");
}

NodePtr StudioShell::terrainRegionTab() {
    std::vector<NodePtr> rows{hstack({button("terrain.region.select", tr("Seleziona"), [this] {
                                          document.regionEditing = !document.regionEditing;
                                          refresh();
                                      }, document.regionEditing ? ButtonRole::primary : ButtonRole::secondary)},
                                     spacing::s2)};
    if (document.regionEditing) {
        rows.push_back(terrainRegionFields());
        rows.push_back(terrainField(tr("Materiale"), terrainMaterials(true, document.regionMaterial, [this](ld::TerrainMaterial m) { document.regionMaterial = m; })));
        rows.push_back(hstack({button("terrain.region.fill", tr("Riempi"), [this] { document.perform(ld::StudioEdit::region(ld::TerrainRegionAction::fill, document.terrainRegion, document.regionMaterial)); }, ButtonRole::primary),
                               button("terrain.region.drain", tr("Svuota acqua"), [this] { document.perform(ld::StudioEdit::region(ld::TerrainRegionAction::clearWater, document.terrainRegion)); }),
                               button("terrain.region.clear", tr("Cancella"), [this] { document.perform(ld::StudioEdit::region(ld::TerrainRegionAction::clear, document.terrainRegion)); }, ButtonRole::destructive)},
                              spacing::s2));
    } else rows.push_back(text(tr("Premi Seleziona e trascina le maniglie per scegliere la zona."), type::caption, colors::textSecondary)->fixed(false, true));
    return vstack(std::move(rows), spacing::s4, HAlign::leading)->fillWidth(Alignment::topLeading())->id("terrain.region");
}

NodePtr StudioShell::toolbox() {
    if (document.assets.empty() && !document.assetsError) document.loadAssets();
    std::string query = ld::trim(document.toolboxQuery);
    std::vector<std::string> keys(toolboxCategories.begin(), toolboxCategories.end());
    std::vector<std::string> titles;
    for (const auto& c : keys) titles.push_back(c == "parts" ? tr("Parti") : tr("Modelli"));
    int chosen = static_cast<int>(std::find(keys.begin(), keys.end(), document.toolboxCategory) - keys.begin());
    auto tabs = segments("toolbox.category", titles, chosen, [this, keys](int i) {
        document.toolboxCategory = keys[static_cast<std::size_t>(i)];
        if (document.toolboxCategory == "models") document.loadAssets();
        refresh();
    }, true, keys);
    auto emptyNote = [](const std::string& message) { return text(message, type::caption, colors::textSecondary)->fixed(false, true)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s4)->fillWidth(Alignment::leading()); };
    NodePtr content;
    bool playing = document.playing;
    if (document.toolboxCategory == "models") {
        if (document.assetsError) {
            content = vstack({text(tr("Risorsa non disponibile."), type::smallStrong, colors::danger), text(*document.assetsError, type::micro, colors::textSecondary)->fixed(false, true), button("toolbox.retry", tr("Riprova"), [this] { document.loadAssets(); })}, spacing::compact, HAlign::leading)->padding(spacing::s4);
        } else {
            std::vector<NodePtr> rows;
            std::vector<ld::StudioAsset> assets;
            for (const auto& asset : document.assets)
                if (query.empty() || ld::localizedContains(asset.name, query)) assets.push_back(asset);
            if (assets.empty()) rows.push_back(emptyNote(document.toolboxQuery.empty() ? tr("Nessun modello importato. Usa Importa per aggiungere un file .obj o .fbx.") : tr("Nessun risultato.")));
            std::string extensions;
            const auto& list = ld::studio_catalog::meshExtensions();
            for (std::size_t i = 0; i < std::min<std::size_t>(4, list.size()); ++i) extensions += (i ? " ." : ".") + list[i];
            std::vector<NodePtr> tiles{toolboxTile("import", "ld-icon-action-import", tr("Importa"), !playing, [this] { importModel(); }, tr("Importa un modello 3D ({0})", extensions))};
            for (const auto& asset : assets) tiles.push_back(toolboxTile(asset.asset, "ld-icon-object-mesh", asset.name, !playing, [this, asset] { document.insertAsset(asset); }, asset.name + " · " + ld::uppercase(asset.kind)));
            rows.push_back(grid(std::move(tiles), metrics::tile + spacing::s4, infinity, spacing::s3, spacing::s4)->padding(spacing::s4));
            content = vstack(std::move(rows), 0);
        }
    } else {
        std::vector<NodePtr> rows;
        std::vector<std::string> kinds;
        for (const auto& kind : ld::studio_catalog::primitives())
            if (query.empty() || ld::localizedContains(ld::studio_catalog::title(kind), query)) kinds.push_back(kind);
        if (kinds.empty()) rows.push_back(emptyNote(tr("Nessun risultato.")));
        std::vector<NodePtr> tiles;
        for (const auto& kind : kinds) tiles.push_back(toolboxTile(kind, ld::studio_catalog::symbol(kind), ld::studio_catalog::title(kind), true, [this, kind] { document.addPart(kind); }));
        rows.push_back(grid(std::move(tiles), metrics::tile + spacing::s4, infinity, spacing::s3, spacing::s4)->padding(spacing::s4));
        content = vstack(std::move(rows), 0)->disabled(playing || static_cast<int>(document.world.blocks.size()) >= ld::studio_limits::parts);
    }
    auto header = vstack({hstack({spacer(0.0), tabs, spacer(0.0)}, 0)->fillWidth(), searchField("toolbox.search", tr("Cerca nella Casella strumenti"), document.toolboxQuery, [this](const std::string& v) { document.toolboxQuery = v; })->fillWidth()}, spacing::s2)
                      ->paddingHorizontal(spacing::s3)
                      ->paddingVertical(spacing::s3);
    return vstack({header, rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels), scroll("toolbox.content", content->paddingVertical(spacing::s2)->fillWidth(Alignment::top()))}, 0)->background(rectangle(colors::surface));
}

NodePtr StudioShell::performance() {
    auto data = document.scriptPerformance();
    double total = 0;
    for (const auto& s : data.scripts) total += s.seconds;
    auto megabytes = [](long long bytes) { return ld::localization::decimal(static_cast<double>(bytes) / 1048576.0, 1) + " MB"; };
    std::string memory;
    for (const auto& [context, bytes] : data.memory) memory += (memory.empty() ? "" : " · ") + std::string(ld::contextTitle(context)) + " " + megabytes(bytes);
    std::string detail = data.scripts.empty() ? tr("nessuna misura") : (document.playing ? tr("in esecuzione") : tr("ultima prova")) + " · " + ld::localization::decimal(data.elapsed, 1) + " s" + (memory.empty() ? "" : " · " + memory);
    auto cell = [](const std::string& value, double width, bool trailing = false) {
        return text(value, console, colors::textPrimary)->frameSize(width, std::nullopt, trailing ? Alignment::trailing() : Alignment::leading());
    };
    auto heading = [](const std::string& value, double width, bool trailing = false) {
        return text(value, type::caption, colors::textSecondary)->frameSize(width, std::nullopt, trailing ? Alignment::trailing() : Alignment::leading());
    };
    std::vector<NodePtr> rows{hstack({heading(tr("Script"), 0)->fillWidth(Alignment::leading()), heading(tr("Contesto"), 70), heading(tr("CPU"), 70, true), heading(tr("Quota"), 56, true), heading(tr("Eventi"), 60, true), heading(tr("Frame"), 60, true)}, spacing::s4)};
    if (data.scripts.empty()) rows.push_back(text(tr("Avvia una prova per vedere quanto lavora ogni script."), type::small, colors::textSecondary)->fillWidth(Alignment::leading()));
    for (const auto& s : data.scripts) {
        double share = total > 0 ? s.seconds / total * 100 : 0;
        rows.push_back(hstack({lineLimited(text(s.script, console, colors::textPrimary), 1)->fillWidth(Alignment::leading()), cell(std::string(ld::contextTitle(s.context)), 70), cell(ld::localization::decimal(s.seconds * 1000, 2) + " ms", 70, true),
                               cell(ld::localization::decimal(share, 1) + "%", 56, true), cell(std::to_string(s.events), 60, true), cell(std::to_string(s.frames), 60, true)},
                              spacing::s4));
    }
    auto table = scroll("studio.performance", vstack(std::move(rows), spacing::s2, HAlign::leading)->padding(spacing::grid)->fillWidth(Alignment::leading()));
    return vstack({panelHeader(tr("Prestazioni script"), detail, "performance"), table->fill()}, 0)->background(rectangle(colors::surface));
}

NodePtr StudioShell::apiBrowser() {
    static const std::vector<lim::tooling::ApiEntry> catalog = lim::tooling::apiCatalog(ld::EngineTypes::shared().registry());
    std::string query = ld::trim(document.apiQuery);
    auto found = lim::tooling::searchApi(catalog, query, 60);
    std::vector<NodePtr> rows;
    if (found.empty()) rows.push_back(text(tr("Nessuna API corrisponde a «{0}».", query), type::small, colors::textSecondary)->fillWidth(Alignment::leading()));
    for (const auto* entry : found) {
        std::string where = entry->owner.empty() ? lim::tooling::apiKindTitle(entry->kind) : tr("{0} di {1}", lim::tooling::apiKindTitle(entry->kind), entry->owner);
        if (entry->only != lim::ExecutionContext::shared) where += " · " + std::string(lim::contextName(entry->only));
        if (entry->deprecated) where += " · " + (entry->replacement.empty() ? tr("deprecato") : tr("deprecato, usa {0}", entry->replacement));
        std::vector<NodePtr> lines{hstack({lineLimited(text(entry->signature, console, entry->deprecated ? colors::textSecondary : colors::textPrimary), 1)->fillWidth(Alignment::leading()), text(where, type::caption, entry->deprecated ? colors::warning : colors::textSecondary)}, spacing::s4)};
        if (!entry->doc.empty()) lines.push_back(lineLimited(text(entry->doc, type::small, colors::textSecondary), 2)->fillWidth(Alignment::leading()));
        rows.push_back(vstack(std::move(lines), borders::pixels, HAlign::leading)->fillWidth(Alignment::leading()));
    }
    std::string detail = query.empty() ? tr("Cerca per nome, classe o descrizione ({0})", static_cast<long long>(catalog.size())) : found.size() == 60 ? tr("più di {0} risultati", 59) : trn("{0} risultato", "{0} risultati", static_cast<long long>(found.size()));
    auto list = scroll("studio.api", vstack(std::move(rows), spacing::s3, HAlign::leading)->padding(spacing::grid)->fillWidth(Alignment::leading()));
    return vstack({panelHeader(tr("API Lim"), detail, "api"), searchField("studio.api.search", tr("Cerca nell’API"), document.apiQuery, [this](const std::string& v) { document.apiQuery = v; })->paddingHorizontal(spacing::compact)->paddingVertical(spacing::s2), ui::divider(), list->fill()}, 0)
        ->background(rectangle(colors::surface));
}

std::pair<long long, long long> StudioShell::outputCounts() const {
    long long errors = 0, warnings = 0;
    for (const auto& line : document.output) {
        if (line.kind == ld::OutputLine::Kind::error) ++errors;
        else if (line.kind == ld::OutputLine::Kind::warning) ++warnings;
    }
    return {errors, warnings};
}

void StudioShell::showOutput() {
    document.panels.insert("output");
    document.utilityTab = "output";
    document.utilityCollapsed = false;
    invalidate();
}

NodePtr StudioShell::output() {
    std::vector<NodePtr> lines;
    for (const auto& message : document.output) {
        Color color = message.kind == ld::OutputLine::Kind::error ? colors::danger : message.kind == ld::OutputLine::Kind::warning ? colors::warning : colors::textPrimary;
        Color band = message.kind == ld::OutputLine::Kind::error ? colors::danger.opacity(0.07) : message.kind == ld::OutputLine::Kind::warning ? colors::warning.opacity(0.08) : Color::clear();
        std::string marker = message.kind == ld::OutputLine::Kind::error ? "✕ " : message.kind == ld::OutputLine::Kind::warning ? "⚠ " : "  ";
        auto line = text(marker + message.stamped(), console, color)->paddingHorizontal(spacing::s4)->paddingVertical(spacing::s1 / 2)->fillWidth(Alignment::leading())->background(rectangle(band));
        line->interactive = true;
        line->interaction.doubleTap = [this, message] {
            document.reveal(message);
            invalidate();
        };
        line->interaction.id = "output.line";
        lines.push_back(line);
    }
    Surface* surface = Surface::active();
    if (surface && document.output.size() != outputCount) {
        outputCount = document.output.size();
        surface->scrollTo("studio.output", {0, 1e9});
    }
    NativeField field;
    field.id = "studio.commandBar";
    field.prompt = tr("Esegui un comando Lim");
    field.text = command;
    field.font = console;
    field.color = colors::textPrimary;
    field.editable = !document.playing;
    field.changed = [this](const std::string& v) { command = v; };
    field.commit = [this](const std::string&) {
        document.runCommand(command);
        command.clear();
        invalidate();
    };
    auto [errors, warnings] = outputCounts();
    std::vector<NodePtr> counts;
    if (errors) counts.push_back(text("✕ " + trn("{0} errore", "{0} errori", errors), type::caption, colors::danger));
    if (warnings) counts.push_back(hstack({ui::icon("ld-icon-status-warning", 13), text(trn("{0} avviso", "{0} avvisi", warnings), type::caption, colors::warning)->fixed(true, false)}, spacing::s1));
    counts.push_back(text(trn("{0} messaggio", "{0} messaggi", static_cast<long long>(document.output.size())), type::caption, colors::textTertiary));
    auto header = hstack({text(document.playing ? tr("esecuzione in corso") : tr("in attesa di una prova"), type::caption, colors::textTertiary), spacer(0.0),
                          hstack(std::move(counts), spacing::s4)->id("output.counts"), plainButton("output.clear", text(tr("Svuota"), type::caption, document.output.empty() ? colors::textDisabled : colors::accent), [this] { document.clearOutput(); })->disabled(document.output.empty())->help(tr("Svuota l’Output"))},
                         spacing::s4)
                      ->paddingHorizontal(spacing::s4)
                      ->frameSize(std::nullopt, metrics::panelHeader - borders::pixels)
                      ->background(rectangle(colors::panelHeader));
    auto log = scroll("studio.output", vstack(std::move(lines), 0, HAlign::leading)->paddingVertical(spacing::s2)->fillWidth(Alignment::leading()));
    auto input = hstack({text("›", console, colors::textTertiary), ui::textField(field)->fill()}, spacing::s2)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::compactControlHeight)->background(rectangle(colors::surfaceAlt));
    return vstack({header, ui::divider(), log->fill(), ui::divider(), input}, 0)->background(rectangle(colors::surface));
}

NodePtr StudioShell::projects() {
    const auto& worlds = store->worlds();
    bool full = static_cast<int>(worlds.size()) >= ld::rules::world::experiencesPerDeveloper;
    std::vector<NodePtr> rows{text("Ludissey Studio", type::pageTitle),
                              hstack({classicTabs("projects.tabs", {tr("Nuovo"), tr("I miei progetti")}, projectsTab, [this](int i) {
                                          projectsTab = i;
                                          macOwner.reset();
                                          invalidate();
                                      })->fillWidth(),
                                      button("projects.refresh", tr("Aggiorna"), [this] { store->perform([owner = store] { return refreshAll(owner); }); })->disabled(store->busy)},
                                     spacing::s4, VAlign::bottom)};
    if (!recoveriesLoaded) {
        recoveries = ld::studio_recovery::list();
        recoveriesLoaded = true;
    }
    for (const auto& entry : recoveries)
        rows.push_back(classicBorder(hstack({glyph("scarica", iconSize::toolbar, colors::warning),
                                             vstack({text(tr("«{0}» ha modifiche non salvate", entry.title), type::smallStrong), text(tr("Copia automatica del {0} alle {1}", ld::localization::date(entry.savedAt), entry.savedAt.size() >= 16 ? entry.savedAt.substr(11, 5) : std::string()) + " · " + trn("{0} oggetto", "{0} oggetti", static_cast<long long>(entry.parts)), type::caption, colors::textSecondary)}, spacing::s1, HAlign::leading), spacer(0.0),
                                             button("recovery.discard." + entry.id, tr("Scarta"), [this, entry] {
                                                 ld::studio_recovery::discard(entry.id);
                                                 recoveriesLoaded = false;
                                                 invalidate();
                                             })->help(tr("Elimina la copia automatica")), button("recovery.open." + entry.id, tr("Recupera"), [this, entry] { recover(entry); }, ButtonRole::primary)->help(tr("Apri il progetto con le modifiche recuperate"))},
                                            spacing::s4)
                                         ->paddingHorizontal(spacing::s8)
                                         ->paddingVertical(spacing::s4)
                                         ->fillWidth(Alignment::leading())
                                         ->background(rectangle(colors::surface))
                                         ->disabled(store->busy)
                                         ->id("recovery." + entry.id),
                                     metrics::cardRadius));
    double side = metrics::tileStandard;
    auto shot = [side](const ui::Image& picture, const std::string& fallback) {
        NodePtr inner = picture.valid() ? ui::picture(picture, true) : ui::icon(fallback, iconSize::ribbon);
        return zstack({rectangle(colors::surfaceAlt), inner})->frameSize(side, std::round(side * 9 / 16))->clipped();
    };
    std::vector<NodePtr> tiles;
    if (projectsTab == 0) {
        for (int kind = 0; kind < 4; ++kind) {
            std::string id = "projects.template." + std::to_string(kind);
            auto face = vstack({shot(templateImage(kind), kind == 0 || kind == 3 ? "ld-icon-object-part" : "ld-icon-studio-terrain"), lineLimited(text(templateTitle(kind), type::smallStrong), 1)->padding(spacing::s4)->fillWidth(Alignment::leading())}, 0, HAlign::leading)->frameSize(side, std::nullopt);
            auto tile = card(plainButton(id, face, [this, kind] { open(templateWorld(kind)); }), id);
            tiles.push_back(full || store->busy ? tile->disabled() : tile);
        }
        if (full) rows.push_back(text(tr("Hai già {0} progetti, il massimo. Eliminane uno per crearne un altro.", ld::rules::world::experiencesPerDeveloper), type::small, colors::warning));
    } else {
        for (const auto& world : worlds) {
            std::error_code missing;
            auto file = ld::studio_assets::preview(world.id);
            ui::Image picture = std::filesystem::is_regular_file(file, missing) ? ui::loadImage(file) : ui::Image{};
            auto more = pullDown("project.menu." + world.id, glyph("altro", iconSize::compact, colors::textPrimary), !store->busy, [this, world] {
                MenuItem openItem, removeItem;
                openItem.title = tr("Apri");
                openItem.action = [this, world] { open(world); };
                removeItem.title = tr("Elimina");
                removeItem.action = [this, id = world.id, title = world.title] {
                    auto owner = store;
                    store->dialog = ld::DialogRequest{tr("Eliminare «{0}»?", title), tr("Sparisce anche da Ludissey. Non si può annullare."), {{tr("Annulla"), ld::DialogRole::secondary, [] {}}, {tr("Elimina"), ld::DialogRole::destructive, [owner, id] { owner->deleteWorld(id); }}}};
                };
                return std::vector<MenuItem>{openItem, removeItem};
            }, tr("Apri o elimina"));
            auto words = hstack({vstack({lineLimited(text(world.title, type::smallStrong), 1), text(world.updatedAt.empty() ? tr("Non ancora salvata") : ld::localization::date(world.updatedAt), type::caption, colors::textSecondary)}, spacing::s1, HAlign::leading), spacer(0.0), more}, spacing::s2)->padding(spacing::s4)->fillWidth(Alignment::leading());
            auto top = plainButton("project.open." + world.id, shot(picture, "ld-icon-object-part"), [this, world] { open(world); });
            tiles.push_back(card(vstack({top, words}, 0, HAlign::leading)->frameSize(side, std::nullopt), "project.open." + world.id)->disabled(store->busy));
        }
        if (worlds.empty() && !macGroup()) rows.push_back(emptyState("ld-icon-action-create", tr("Non hai ancora progetti. Scegline uno da «Nuovo».")));
    }
    if (!tiles.empty()) rows.push_back(grid(std::move(tiles), side, side, spacing::s8, spacing::s8)->id("projects.grid"));
    if (projectsTab == 1)
        if (auto local = macGroup()) rows.push_back(local);
    return scroll("studio.projects", vstack(std::move(rows), spacing::s8, HAlign::leading)->padding(spacing::s12)->fillWidth(Alignment::topLeading()))->background(rectangle(colors::canvas));
}

NodePtr StudioShell::macGroup() {
    std::string username = store->profile ? store->profile->username : "";
    if (macOwner != username) {
        macProjects = ld::mac_archive::projects(username);
        macOwner = username;
    }
    if (macProjects.empty()) return nullptr;
    std::vector<NodePtr> rows;
    for (const auto& world : macProjects) {
        bool uploaded = std::any_of(store->worlds().begin(), store->worlds().end(), [&](const ld::Experience& online) { return online.id == world.id; });
        NodePtr accessory;
        if (uploaded)
            accessory = hstack({text(tr("Caricata"), type::small, colors::textSecondary), button("mac.remove." + world.id, tr("Elimina dal Mac"), [this, id = world.id, title = world.title] {
                                    store->dialog = ld::DialogRequest{tr("Eliminare «{0}» dal Mac?", title), tr("La copia su Ludissey resta. Non si può annullare."), {{tr("Annulla"), ld::DialogRole::secondary, [] {}}, {tr("Elimina"), ld::DialogRole::destructive, [this, id] {
                                                                           if (store->profile) ld::mac_archive::remove(store->profile->username, id);
                                                                           macOwner.reset();
                                                                           invalidate();
                                                                       }}}};
                                    invalidate();
                                }, ButtonRole::destructive, ButtonSize::small)},
                               spacing::s4);
        else
            accessory = button("mac.upload." + world.id, tr("Carica sul tuo account"), [this, world] { store->perform([owner = store, world] { return uploadFromMac(owner, world); }); }, ButtonRole::primary, ButtonSize::small)->disabled(store->busy);
        rows.push_back(groupRow(text(world.title), accessory, trn("{0} oggetto", "{0} oggetti", static_cast<long long>(world.blocks.size())))->id("mac.project." + world.id));
    }
    return vstack({groupTitle(tr("Su questo Mac")), group(std::move(rows)), groupNote(tr("Esperienze salvate solo su questo Mac. Caricale sul tuo account per ritrovarle ovunque."))}, spacing::s2, HAlign::leading)->fillWidth(Alignment::leading())->id("mac.group");
}

NodePtr StudioShell::imageSetting(ld::ExperienceImage kind) {
    bool icon = kind == ld::ExperienceImage::icon;
    std::string key = icon ? "settings.icon" : "settings.thumbnail";
    auto file = document.experienceImageFile(kind);
    double height = metrics::controlHeight * 2, width = icon ? height : std::round(height * 16 / 9);
    Image picture = file ? loadImage(*file) : Image{};
    NodePtr preview = picture.valid() ? ui::picture(picture, true) : vstack({ui::icon("ld-icon-action-import", iconSize::compact)->opacity(0.5), text(tr("Nessuna immagine"), type::micro, colors::textTertiary)}, spacing::s1)->padding(spacing::s2);
    preview = zstack({rectangle(colors::surfaceAlt), preview})->frameSize(width, height)->clipped(metrics::smallRadius)->overlay(strokeRectangle(colors::border, borders::pixels, metrics::smallRadius))->id(key + ".preview");
    auto upload = button(key + ".upload", tr("Carica…"), [this, kind] {
        if (!chooseContent) return;
        auto chosen = chooseContent({"png", "jpg", "jpeg"});
        if (!chosen) return;
        auto result = document.setExperienceImage(kind, *chosen);
        if (!result.ok()) store->error = result.error().message;
        invalidate();
    });
    auto download = button(key + ".download", tr("Scarica…"), [this, kind, icon] {
        auto source = document.experienceImageFile(kind);
        if (!source || !chooseDestination) return;
        std::string extension = source->extension().string().empty() ? "png" : source->extension().string().substr(1);
        auto target = chooseDestination(document.world.title + " · " + (icon ? tr("icona") : tr("miniatura")) + "." + extension, extension);
        if (!target) return;
        std::error_code problem;
        std::filesystem::copy_file(*source, *target, std::filesystem::copy_options::overwrite_existing, problem);
        if (problem) store->error = tr("Impossibile salvare l’immagine: {0}", problem.message());
        else document.log(icon ? tr("Icona salvata in {0}", target->filename().string()) : tr("Miniatura salvata in {0}", target->filename().string()), false);
        invalidate();
    });
    auto remove = button(key + ".remove", tr("Rimuovi"), [this, kind] {
        document.clearExperienceImage(kind);
        invalidate();
    });
    bool editable = !document.playing;
    auto hint = text(icon ? tr("Quadrata, consigliata 512 × 512") : tr("Formato 16:9, consigliata 1280 × 720"), type::caption, colors::textTertiary);
    auto controls = vstack({hstack({(editable ? upload : upload->disabled())->fixed(), (file ? download : download->disabled())->fixed(), (file && editable ? remove : remove->disabled())->fixed()}, spacing::s3), hint->fixed(false, true)}, spacing::s2, HAlign::leading);
    return hstack({preview, controls, spacer(0.0)}, spacing::s6, VAlign::center);
}

NodePtr StudioShell::settings() {
    auto details = store->details.find(document.world.id);
    const ld::ExperienceFacts* facts = details == store->details.end() ? nullptr : &details->second.experience;
    auto settingRow = [](const std::string& title, NodePtr content) { return hstack({text(title, type::small, colors::textSecondary)->frameSize(metrics::propertyName + spacing::s12 + spacing::s4, std::nullopt, Alignment::leading()), content, spacer(0.0)}, spacing::s4); };
    auto value = [](const std::string& t) { return text(t, type::small, colors::textSecondary); };
    NativeField genre;
    genre.id = "settings.genre";
    int selected = 0;
    for (std::size_t i = 0; i < ld::rules::genres.size(); ++i) {
        genre.options.push_back(ld::genreTitle(ld::rules::genres[i]));
        if (document.world.genre == ld::rules::genres[i]) selected = static_cast<int>(i);
    }
    genre.selectedOption = selected;
    genre.choose = [this](int i) { document.configure(std::nullopt, std::string(ld::rules::genres[static_cast<std::size_t>(i)]), std::nullopt); };
    auto format = [](const std::optional<std::string>& raw) {
        if (!raw || raw->empty()) return std::string("—");
        return ld::localization::date(*raw);
    };
    bool published = facts && facts->published;
    NodePtr visibility = hstack({text(published ? tr("Pubblica: visibile e giocabile in Ludissey") : tr("Privata: solo tu puoi aprirla in Studio"), type::small, published ? colors::success : colors::textSecondary)->id("settings.visibility.state"),
                                 published ? button("settings.unpublish", tr("Rendi privata"), [this] {
                                                 auto owner = store;
                                                 auto id = document.world.id;
                                                 store->dialog = ld::DialogRequest{tr("Rendere privata l’esperienza?"), tr("L’esperienza sparirà da Ludissey. Il progetto resta tuo e puoi ripubblicarlo quando vuoi."),
                                                                                   {{tr("Annulla"), ld::DialogRole::secondary, [] {}}, {tr("Rendi privata"), ld::DialogRole::destructive, [owner, id] { owner->unpublish(id); }}}};
                                                 invalidate();
                                             })->disabled(store->busy || document.playing || document.world.revision == 0)
                                           : button("settings.publish", tr("Pubblica"), [this] { save(true); }, ButtonRole::primary)->disabled(store->busy || document.playing)},
                                spacing::s4);
    auto body = vstack({settingRow(tr("Nome"), classicField("settings.title", tr("Nome"), document.world.title, [this](const std::string& v) { document.configure(v, std::nullopt, std::nullopt); })), settingRow(tr("Genere"), select(genre)),
                        settingRow(tr("Giocatori per server"), hstack({numberField("settings.serverSize", document.world.serverSize, ld::rules::serverSize::minimum, ld::rules::serverSize::maximum, 1, 0, [this](double v) { document.configure(std::nullopt, std::nullopt, static_cast<int>(std::round(v))); })->frameSize(120, std::nullopt),
                                                                    text(tr("massimo per ogni server"), type::micro, colors::textSecondary)},
                                                                   spacing::compact)),
                        settingRow(tr("Icona"), imageSetting(ld::ExperienceImage::icon)), settingRow(tr("Miniatura"), imageSetting(ld::ExperienceImage::thumbnail)),
                        settingRow(tr("Visibilità"), visibility),
                        ui::divider()->paddingVertical(spacing::tight), settingRow(tr("Sviluppatore"), value(document.world.author.empty() ? (store->profile ? store->profile->username : "") : document.world.author)),
                        settingRow(tr("Creata"), value(!document.world.createdAt && document.world.revision == 0 ? tr("Al primo salvataggio") : format(document.world.createdAt))),
                        settingRow(tr("Aggiornata"), value(document.world.updatedAt.empty() ? tr("Non ancora salvata") : format(document.world.updatedAt))), settingRow(tr("Visite"), value(facts ? ld::localization::number(facts->visits) : "—")),
                        text(tr("Le modifiche valgono per i nuovi server, dopo che pubblichi."), type::micro, colors::textSecondary)->fixed(false, true)},
                       spacing::inlineGap, HAlign::leading)
                    ->padding(spacing::s8);
    auto header = hstack({text(tr("Impostazioni esperienza"), type::dialogTitle), spacer(), plainButton("settings.close", glyph("chiudi", iconSize::compact, colors::textSecondary)->frameSize(iconSize::toolbar, iconSize::toolbar), [this] {
                                                                                     document.settingsOpen = false;
                                                                                     invalidate();
                                                                                 })->help(tr("Chiudi"))})
                      ->paddingHorizontal(spacing::s8)
                      ->frameSize(std::nullopt, metrics::controlHeight + spacing::s4)
                      ->background(rectangle(colors::surface));
    auto footer = hstack({spacer(), button("settings.done", tr("Fine"), [this] {
                                        document.settingsOpen = false;
                                        invalidate();
                                    }, ButtonRole::primary)})
                      ->padding(spacing::s4);
    auto panel = classicBorder(vstack({header, ui::divider(), body, ui::divider(), footer->background(rectangle(colors::surfaceAlt))}, 0, HAlign::leading)->frameSize(metrics::dialogWidth + spacing::s16 * 4, std::nullopt)->background(rectangle(colors::surface))->clipped(metrics::dialogRadius), metrics::dialogRadius)->boxShadow(drops::popover.color, 0, drops::popover.dy, drops::popover.blur, metrics::dialogRadius);
    return zstack({rectangle(Color::black(shadows::scrim))->onTap("settings.scrim", [] {}), panel})->fill()->id("studio.settings");
}

void StudioShell::tick(double now, double delta) {
    if (publishing && !store->busy) publishing = false;
    if (afterSave && !store->busy) {
        if (document.opened && !document.dirty()) {
            auto next = std::move(afterSave);
            afterSave = nullptr;
            next();
            invalidate();
        } else if (store->error) afterSave = nullptr;
    }
    if (!document.opened) return;
    saveLayout(now);
    document.tick(now);
    if (stage) stage->tick(now, delta);
    if (!document.playing) {
        bool dirty = document.dirty();
        if (dirty && (recoveredAt < 0 || now - recoveredAt >= ld::studio_recovery::interval)) {
            recoveredAt = now;
            if (auto written = ld::studio_recovery::write(document.world, localStamp()); !written) ld::Log::warning("Studio", written.error().message);
        } else if (!dirty && recoveredAt >= 0) {
            discardRecovery();
            recoveredAt = -1;
        }
    }
    if (deferredRefresh && (!stage || !stage->interacting() || (!stage->painting() && now - deferredShown >= 0.1))) {
        deferredRefresh = false;
        deferredShown = now;
        invalidate();
    }
    if (document.playing && document.panels.count("performance") && now - performanceShown >= 0.5) {
        performanceShown = now;
        invalidate();
    }
}

std::vector<ld::render::View> StudioShell::views(const std::vector<ViewportSlot>& slots) {
    std::vector<ld::render::View> out;
    if (!stage) return out;
    for (const auto& slot : slots) {
        if (slot.id != "studio.viewport") continue;
        stage->size = {slot.frame.width, slot.frame.height};
        ld::render::View v;
        v.key = slot.id;
        v.scene = stage->scene();
        v.camera = stage->camera();
        v.x = slot.frame.x;
        v.y = slot.frame.y;
        v.width = slot.frame.width;
        v.height = slot.frame.height;
        v.quality = stage->quality();
        out.push_back(v);
    }
    return out;
}

bool StudioShell::pointer(const std::string& slot, const PointerEvent& event) {
    if (slot != "studio.viewport" || !stage) return false;
    if (explorerActive && event.phase == PointerPhase::down) {
        explorerActive = false;
        invalidate();
    }
    return stage->pointer(event, stage->size);
}

void StudioShell::resigned() {
    if (stage) stage->resigned();
}

bool StudioShell::key(const KeyInput& key) {
    if (key.down && key.keyCode == 53 && !document.playing && document.terrainEditing && (document.terrainTool || document.terrainMode || document.regionEditing)) {
        document.closeTerrainTool();
        refresh();
        return true;
    }
    if (key.down && key.command) {
        std::string c = ld::lowercase(key.characters);
        auto run = [this](ld::StudioCommand command) {
            if (document.can(command)) document.run(command);
            invalidate();
            return true;
        };
        if (c == "z") return run(key.shift ? ld::StudioCommand::redo : ld::StudioCommand::undo);
        if (c == "c") return run(ld::StudioCommand::copy);
        if (c == "x") return run(ld::StudioCommand::cut);
        if (c == "v") return run(ld::StudioCommand::paste);
        if (c == "d") return run(ld::StudioCommand::duplicate);
        if (c == "g") return run(ld::StudioCommand::group);
        if (c == "u" && key.shift) return run(ld::StudioCommand::ungroup);
        if (c == "a") return run(ld::StudioCommand::selectAll);
        if (c == "l") return run(ld::StudioCommand::lock);
        if (c == "r") return run(ld::StudioCommand::rotate);
        if (c == "t") return run(ld::StudioCommand::tilt);
        if (c == "s") {
            save(false);
            return true;
        }
        std::map<std::string, ld::StudioTool> tools{{"1", ld::StudioTool::select}, {"2", ld::StudioTool::move}, {"3", ld::StudioTool::scale}, {"4", ld::StudioTool::rotate}};
        if (auto found = tools.find(c); found != tools.end()) {
            document.tool = found->second;
            document.terrainTool.reset();
            refresh();
            return true;
        }
        return false;
    }
    if (key.down && !key.command) {
        if (key.keyCode == 96) {
            ld::StudioCommand command = key.shift ? ld::StudioCommand::stop : ld::StudioCommand::play;
            if (document.can(command)) document.run(command);
            invalidate();
            return true;
        }
        if (key.keyCode == 100) {
            if (document.can(ld::StudioCommand::run)) document.run(ld::StudioCommand::run);
            invalidate();
            return true;
        }
        if (key.keyCode == 120) {
            if (auto selection = document.selection(); selection && !selection->service) document.renaming = selection->id;
            invalidate();
            return true;
        }
    }
    if (stage && stage->key(key)) return true;
    if (key.down && key.keyCode == 51 && !key.command) {
        if (document.can(ld::StudioCommand::remove)) document.run(ld::StudioCommand::remove);
        invalidate();
        return true;
    }
    return false;
}

}
