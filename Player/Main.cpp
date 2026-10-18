#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/Diagnostics.hpp"
#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/DataModel/ClassRegistry.hpp"
#include "Interface/ClientMedia.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Components.hpp"
#include "Localization/Localization.hpp"
#include "Platform/Mac/Platform.hpp"
#include "Player/PlayerView.hpp"
#include "Studio/StudioView.hpp"

#include <filesystem>
#include <memory>

using ld::tr;

namespace {

struct Paths {
    std::filesystem::path resources;
    std::filesystem::path registry;
    std::filesystem::path rules;
};

Paths locate() {
    Paths p;
    p.resources = ld::resourceFolder();
    std::filesystem::path tree = p.resources.parent_path();
    auto pick = [](const std::filesystem::path& bundled, const std::filesystem::path& source) { return std::filesystem::exists(bundled) ? bundled : source; };
    p.registry = pick(p.resources / "Registry/2017.json", tree / "HistoricalAPI/2017.json");
    p.rules = pick(p.resources / "Registry/class-rules.json", p.resources / "class-rules.json");
    return p;
}

class Application {
public:
    void launch() {
        paths = locate();
        if (auto registry = ld::ClassRegistry::load(paths.registry, paths.rules)) ld::ClassRegistry::setShared(*registry);
        auto playfab = std::make_shared<ld::PlayFab>();
        auto backend = ld::RoutedBackend::forPlayFab(playfab);
        store = std::make_shared<ld::Store>(playfab, backend);
        store->quit = [] { platform::terminate(); };
        client = std::make_unique<ui::ClientView>(store);
        media = std::make_unique<ui::ClientMedia>(store);
        player = std::make_unique<player::PlayerShell>(store);
        studioShell = std::make_unique<studio::StudioShell>(store);
        ld::audio::setMasterVolume(ld::local_preferences::number(ld::local_preferences::keys::masterVolume).value_or(1));
        ld::render::scene_factory::warm();
        bool studio = ld::ludissey_target::isStudio();
        window = std::make_unique<platform::Window>(platform::WindowSpec{studio ? "Ludissey Studio" : "Ludissey", 1180, 760, 860, 680}, [this] { return client->build(); });
        client->focusField = [this](const std::string& id) {
            if (window) window->focusField(id);
        };
        auto refresh = [this] {
            if (window) window->invalidate();
            if (insertWindow) insertWindow->invalidate();
        };
        client->invalidated = refresh;
        client->thumbnail = [this](const std::string& id) { return media->thumbnail(id); };
        client->experienceIcon = [this](const std::string& id) { return media->icon(id); };
        client->itemImage = [this](const ld::AvatarItem& item) { return media->itemImage(item); };
        client->portrait = [this](const std::string& username) { return media->portrait(username); };
        client->figure = [this](const std::string& id) { return media->figure(id); };
        store->renderPortraits = [this](const ld::AvatarConfiguration& look) { return media->portraitImages(look); };
        client->playerShell = [this] { return player->build(); };
        player->experienceIcon = [this](const std::string& id) { return media->icon(id); };
        client->graphicsChanged = [this](std::optional<int> level) { player->chooseGraphics(level); };
        client->studioShell = [this] { return studioShell->build(); };
        studioShell->invalidated = refresh;
        studioShell->setTitle = [this](const std::string& title) { window->setTitle(title); };
        studioShell->setEdited = [this](bool edited) { window->setEdited(edited); };
        studioShell->chooseMesh = [] { return platform::chooseFile(ld::studio_catalog::meshExtensions(), tr("Importa modello 3D")); };
        studioShell->chooseContent = [](const std::vector<std::string>& extensions) { return platform::chooseFile(extensions, tr("Importa risorsa")); };
        studioShell->chooseDestination = [](const std::string& name, const std::string& extension) { return platform::chooseDestination(name, extension); };
        studioShell->showSheet = [this](bool visible) { pendingSheet = visible; };
        window->rendered = [this](const ui::Surface& surface) {
            studioShell->rendered(surface);
            if (!pendingSheet) return;
            bool visible = *pendingSheet;
            pendingSheet.reset();
            if (visible) {
                if (!insertWindow) insertWindow = std::make_unique<platform::Window>(platform::WindowSpec{tr("Inserisci oggetto"), 360, 390, 360, 390, true}, [this] { return studioShell->insertSheet(); });
                window->beginSheet(*insertWindow);
                insertWindow->focusField("studio.insertObject.search");
            } else if (insertWindow) {
                window->endSheet(*insertWindow);
                window->focusViewport();
            }
        };
        player->invalidated = refresh;
        player->focusField = [this](const std::string& id) { window->focusField(id); };
        player->focusViewport = [this] { window->focusViewport(); };
        store->changed = refresh;
        ld::LaunchRequest::shared().changed = refresh;
        store->openUrl = [](const std::string& url) { platform::openUrl(url); };
        store->launchStudio = [this, refresh](std::optional<std::string> project) {
            auto bundle = platform::bundlePath().parent_path() / "Ludissey Studio.app";
            if (!std::filesystem::exists(bundle)) {
                store->error = tr("Non trovo Ludissey Studio. Mettilo nella cartella Applicazioni, accanto a Ludissey.");
                refresh();
                return;
            }
            if (!project || project->empty()) platform::openApplication(bundle, {});
            else platform::openApplication(bundle, {"ludissey-studio://open?project=" + *project});
        };
        window->views = [this](const std::vector<ui::ViewportSlot>& slots) {
            auto views = media->views(slots);
            auto world = player->views(slots);
            views.insert(views.end(), world.begin(), world.end());
            auto editing = studioShell->views(slots);
            views.insert(views.end(), editing.begin(), editing.end());
            return views;
        };
        window->tick = [this](double now, double delta) {
            media->tick(delta);
            player->tick(now, delta);
            if (ld::ludissey_target::isStudio() && store->profile) studioShell->tick(now, delta);
        };
        window->pointer = [this](const std::string& slot, const ui::PointerEvent& event) {
            if (player->pointer(slot, event)) return true;
            if (studioShell->pointer(slot, event)) return true;
            if (event.phase == ui::PointerPhase::dragged) media->orbit(slot, event.delta.x, event.delta.y);
            else if (event.phase == ui::PointerPhase::scroll) media->zoom(slot, event.scrollY);
            else return false;
            return true;
        };
        window->keys = [this](const ui::KeyInput& key) {
            if (!store->profile || store->error || store->dialog) return false;
            if (ld::ludissey_target::isStudio()) {
                if (studioShell->document.settingsOpen || studioShell->document.insertTarget) return false;
                if (window->editingText() && !(key.down && ((key.command && (key.characters == "s" || key.characters == "S")) || (!key.command && (key.keyCode == 96 || key.keyCode == 100))))) return false;
                return studioShell->key(key);
            }
            if (window->editingText()) return false;
            return player->active() ? player->key(key) : false;
        };
        window->resignedViewport = [this] {
            player->resigned();
            studioShell->resigned();
        };
        window->closing = [this] { return mayQuit(); };
        studioShell->quit = [] { platform::terminate(); };
        window->show();
        if (studio) store->navigate(ld::Route::experiences, ld::ExperienceScope::mine);
        store->restoreSession().start();
    }

    bool mayQuit() {
        if (!ld::ludissey_target::isStudio() || !studioShell || !store || !store->profile) return true;
        bool allowed = studioShell->requestQuit();
        if (!allowed && window) {
            window->show();
            window->invalidate();
        }
        return allowed;
    }

    void about() {
        if (!store) return;
        bool studio = ld::ludissey_target::isStudio();
        auto licenses = ld::resourceFolder() / "Licenses";
        store->dialog = ld::DialogRequest{studio ? "Ludissey Studio" : "Ludissey", tr("Versione {0}", platform::version()) + "\n" + tr("Ludissey. Tutti i diritti riservati.") + "\n" + tr("Le licenze dei componenti di terze parti sono incluse nell’app."),
                                          {{tr("Licenze"), ld::DialogRole::secondary, [licenses] { platform::openUrl("file://" + licenses.string()); }}, {tr("OK"), ld::DialogRole::primary, [] {}}}};
        if (window) window->invalidate();
    }

    ld::StudioCommand lockCommand() const { return studioShell && studioShell->document.can(ld::StudioCommand::unlock) ? ld::StudioCommand::unlock : ld::StudioCommand::lock; }

    bool signedIn() const { return store && store->profile && !store->dialog && !store->error; }

    void studioCommand(ld::StudioCommand command, const char* textSelector) {
        if (textSelector && window && window->editingText()) {
            platform::sendAction(textSelector);
            return;
        }
        if (!signedIn() || !studioShell->document.opened) return;
        if (studioShell->document.can(command)) studioShell->document.run(command);
        if (window) window->invalidate();
    }

    template <typename Action>
    void studio(Action action) {
        if (!signedIn()) return;
        action(*studioShell);
        if (window) window->invalidate();
    }

    void open(const std::string& url) {
        ld::LaunchRequest::shared().accept(url);
        if (window) window->invalidate();
    }

    void reopen() {
        if (window) window->show();
    }

    void terminate() {
        ld::LaunchRequest::shared().changed = nullptr;
        if (store) {
            store->changed = nullptr;
            store->launchStudio = nullptr;
        }
        if (window && insertWindow) window->endSheet(*insertWindow);
        insertWindow.reset();
        window.reset();
        if (player) player->invalidated = nullptr;
        if (studioShell) studioShell->invalidated = nullptr;
        player.reset();
        studioShell.reset();
    }

private:
    Paths paths;
    std::shared_ptr<ld::Store> store;
    std::unique_ptr<ui::ClientView> client;
    std::unique_ptr<ui::ClientMedia> media;
    std::unique_ptr<player::PlayerShell> player;
    std::unique_ptr<studio::StudioShell> studioShell;
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<platform::Window> insertWindow;
    std::optional<bool> pendingSheet;
};

}

int main() {
    std::string identifier = platform::bundleIdentifier();
    if (!identifier.empty()) ld::ludissey_target::setCurrent(identifier);
    if (!platform::singleInstance(identifier.empty() ? ld::ludissey_target::client : identifier)) return 0;
    ui::theme::applyScale(ld::ludissey_target::isStudio() ? ui::theme::Scale::studio : ui::theme::Scale::client);
    if (auto chosen = ld::local_preferences::number(ld::local_preferences::keys::language); chosen && *chosen >= 0) ld::localization::setLanguage(*chosen < 1 ? ld::Language::italian : ld::Language::english);
    ld::diagnostics::install(ld::supportFolder() / "Diagnostica", ld::ludissey_target::isStudio() ? "Ludissey Studio" : "Ludissey");
    ld::Log::info("App", "avvio");
    static Application application;
    platform::Application app;
    app.bundleIdentifier = identifier;
    bool studio = ld::ludissey_target::isStudio();
    app.menus = {{"", {{tr("Informazioni su {0}", studio ? "Ludissey Studio" : "Ludissey"), "", "", [] { application.about(); }}, {tr("Esci da {0}", studio ? "Ludissey Studio" : "Ludissey"), "q", "terminate:", nullptr}}}};
    if (studio) {
        using ld::StudioCommand;
        app.menus.push_back({tr("File"), {{tr("Nuova esperienza"), "n", "", [] { application.studio([](studio::StudioShell& s) { s.newProject(); }); }},
                                      {tr("Apri esperienza…"), "o", "", [] { application.studio([](studio::StudioShell& s) { s.showProjects(); }); }},
                                      {tr("Salva"), "s", "", [] { application.studio([](studio::StudioShell& s) { s.save(false); }); }},
                                      {tr("Salva come…"), "S", "", [] { application.studio([](studio::StudioShell& s) { s.saveAs(); }); }},
                                      {tr("Pubblica"), "", "", [] { application.studio([](studio::StudioShell& s) { s.save(true); }); }},
                                      {tr("Impostazioni esperienza…"), "", "", [] { application.studio([](studio::StudioShell& s) { s.openSettings(); }); }},
                                      {tr("Importa modello 3D…"), "", "", [] { application.studio([](studio::StudioShell& s) { s.importModel(); }); }},
                                      {tr("Chiudi progetto"), "w", "", [] { application.studio([](studio::StudioShell& s) { s.requestClose(); }); }}}});
        app.menus.push_back({tr("Modifica"), {{tr("Annulla@@modifica"), "z", "", [] { application.studioCommand(StudioCommand::undo, "undo:"); }},
                                          {tr("Ripeti"), "Z", "", [] { application.studioCommand(StudioCommand::redo, "redo:"); }},
                                          {tr("Taglia"), "x", "", [] { application.studioCommand(StudioCommand::cut, "cut:"); }},
                                          {tr("Copia"), "c", "", [] { application.studioCommand(StudioCommand::copy, "copy:"); }},
                                          {tr("Incolla"), "v", "", [] { application.studioCommand(StudioCommand::paste, "paste:"); }},
                                          {tr("Duplica"), "d", "", [] { application.studioCommand(StudioCommand::duplicate, nullptr); }},
                                          {tr("Elimina"), "", "", [] { application.studioCommand(StudioCommand::remove, nullptr); }},
                                          {tr("Seleziona tutto"), "a", "", [] { application.studioCommand(StudioCommand::selectAll, "selectAll:"); }},
                                          {tr("Raggruppa in un modello"), "g", "", [] { application.studioCommand(StudioCommand::group, nullptr); }},
                                          {tr("Separa il modello"), "U", "", [] { application.studioCommand(StudioCommand::ungroup, nullptr); }},
                                          {tr("Ruota di 90°"), "r", "", [] { application.studioCommand(StudioCommand::rotate, nullptr); }},
                                          {tr("Inclina di 90°"), "t", "", [] { application.studioCommand(StudioCommand::tilt, nullptr); }},
                                          {tr("Blocca o sblocca"), "l", "", [] { application.studioCommand(application.lockCommand(), nullptr); }},
                                          {tr("Appoggia al suolo"), "", "", [] { application.studio([](studio::StudioShell& s) { s.document.dropToFloor(); }); }}}});
        app.menus.push_back({tr("Vista"), {{tr("Esplora"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("explorer"); }); }},
                                       {tr("Proprietà"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("properties"); }); }},
                                       {tr("Casella strumenti"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("toolbox"); }); }},
                                       {tr("Output"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("output"); }); }},
                                       {tr("Prestazioni script"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("performance"); }); }},
                                       {tr("API Lim"), "", "", [] { application.studio([](studio::StudioShell& s) { s.togglePanel("api"); }); }},
                                       {tr("Griglia"), "", "", [] { application.studio([](studio::StudioShell& s) { s.toggleView("grid"); }); }},
                                       {tr("Interfaccia di gioco"), "", "", [] { application.studio([](studio::StudioShell& s) { s.toggleView("gui"); }); }},
                                       {tr("Inquadra la selezione"), "", "", [] { application.studio([](studio::StudioShell& s) { s.frameSelection(); }); }},
                                       {tr("Reimposta layout"), "", "", [] { application.studio([](studio::StudioShell& s) { s.resetLayout(); }); }}}});
        app.menus.push_back({tr("Aiuto"), {{tr("Scorciatoie da tastiera"), "?", "", [] { application.studio([](studio::StudioShell& s) { s.showHelp(); }); }}}});
    } else app.menus.push_back({tr("Modifica"), {{tr("Taglia"), "x", "cut:", nullptr}, {tr("Copia"), "c", "copy:", nullptr}, {tr("Incolla"), "v", "paste:", nullptr}, {tr("Seleziona tutto"), "a", "selectAll:", nullptr}}});
    app.quitting = [] { return application.mayQuit(); };
    app.launched = [] { application.launch(); };
    app.openUrl = [](const std::string& url) { application.open(url); };
    app.reopen = [] { application.reopen(); };
    app.terminating = [] { application.terminate(); };
    return platform::run(std::move(app));
}
