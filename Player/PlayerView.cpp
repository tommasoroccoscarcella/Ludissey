#include "PlayerView.hpp"

#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Core/Text.hpp"
#include "Interface/Surface.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

namespace player {

using namespace ui;
using namespace ui::theme;
using ld::tr;

namespace {

double wallClock() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

ld::Async<void> replace(std::shared_ptr<ld::PlayerSession> previous, std::shared_ptr<ld::PlayerSession> next) {
    if (previous) co_await previous->leave();
    co_await next->start();
}

ld::Async<void> close(std::shared_ptr<ld::PlayerSession> closing, std::shared_ptr<ld::Store> store) {
    co_await closing->leave();
    co_await store->loadDetails(closing->ticket.experienceId.text());
}

ld::Async<void> send(std::shared_ptr<ld::PlayerSession> session, std::string text) { co_await session->send(std::move(text)); }

ld::Async<void> exchange(std::shared_ptr<ld::Backend> backend, ld::Json request, ld::ScriptEngine::StorageReply reply) {
    std::optional<ld::Json> value;
    std::string error;
    try {
        ld::Json result = co_await backend->cloud("LudisseyExperienceData", std::move(request));
        value = result["value"];
    } catch (const std::exception& e) {
        error = e.what();
        if (error.empty()) error = tr("Salvataggi non disponibili al momento");
    }
    co_await ld::onMain();
    reply(std::move(value), std::move(error));
}

Color white(double alpha = 1) { return colors::hudText.opacity(alpha); }

Color panel() { return colors::hud.opacity(shadows::hud); }

const Font console = Font::mono(ui::tokens::font::caption);

}

PlayerShell::PlayerShell(std::shared_ptr<ld::Store> value) : store(std::move(value)) {
    overlay.invalidated = [this] { invalidate(); };
    if (auto chosen = ld::local_preferences::number(ld::local_preferences::keys::graphicsChoice); chosen && *chosen >= 0) quality.choose(static_cast<int>(*chosen));
}

void PlayerShell::chooseGraphics(std::optional<int> level) {
    quality.choose(level);
    invalidate();
}

PlayerShell::~PlayerShell() {
    scene.reset();
    if (session) {
        session->changed = nullptr;
        session->leave().start();
    }
}

void PlayerShell::invalidate() {
    if (invalidated) invalidated();
}

void PlayerShell::begin() {
    auto& launch = ld::LaunchRequest::shared();
    if (!launch.joinTicket || current == launch.joinTicket) return;
    current = launch.joinTicket;
    auto previous = session;
    if (previous) previous->changed = nullptr;
    scene.reset();
    signature.clear();
    auto next = std::make_shared<ld::PlayerSession>(*launch.joinTicket, store->backend, std::make_shared<ld::AssetService>(store->backend));
    next->gameplay = gameplay;
    auto look = store->currentAvatar();
    next->displayName = look ? look->displayName : "";
    next->avatar = look;
    next->meshReadable = [](const std::filesystem::path& path) { return ld::render::scene_factory::mesh(path) != nullptr; };
    next->changed = [this] { invalidate(); };
    session = next;
    gameplay->menuOpen = false;
    confirmingLeave = false;
    replace(previous, next).start();
}

void PlayerShell::leave() {
    auto closing = session;
    if (closing) closing->changed = nullptr;
    scene.reset();
    signature.clear();
    session.reset();
    current.reset();
    confirmingLeave = false;
    gameplay->leaveRequested = false;
    gameplay->menuOpen = false;
    gameplay->chatOpen = false;
    gameplay->chatFocused = false;
    ld::LaunchRequest::shared().setTicket(std::nullopt);
    if (closing) close(closing, store).start();
    invalidate();
}

void PlayerShell::prepareScene() {
    if (!session || !session->ready() || !session->world()) return;
    auto look = store->currentAvatar();
    if (!look) return;
    const auto& world = *session->world();
    std::string next = world.id + "." + std::to_string(world.revision) + "." + world.updatedAt + "." + std::to_string(look->signature());
    if (next == signature && scene) return;
    signature = next;
    scene.reset();
    std::weak_ptr<ld::PlayerSession> owner = session;
    std::string experienceId = world.id;
    auto storage = [owner, experienceId](const std::string& action, const ld::Json& value, ld::ScriptEngine::StorageReply reply) {
        auto active = owner.lock();
        if (!active) {
            reply(std::nullopt, tr("sessione terminata"));
            return;
        }
        ld::Json request = ld::Json::object({{"experienceId", experienceId}, {"action", action}});
        if (action == "save") request.set("value", value);
        ld::spawnDetached(exchange(active->backend, std::move(request), std::move(reply)));
    };
    scene = std::make_unique<ld::render::PlayerScene>(world, *look, gameplay, std::move(storage));
    scene->changed = [this] { invalidate(); };
    quality.restart(clock);
    if (focusViewport) focusViewport();
}

void PlayerShell::tick(double now, double delta) {
    clock = now;
    std::string before = std::to_string(gameplay->health) + "|" + std::to_string(gameplay->dead) + "|" + std::to_string(gameplay->menuOpen) + "|" + std::to_string(gameplay->chatFocused) + "|" + std::to_string(gameplay->chatOpen) + "|" + std::to_string(gameplay->console.size()) + "|" + std::to_string(gameplay->tools.size()) + "|" +
                         std::to_string(gameplay->loaded) + "|" + std::to_string(gameplay->leaveRequested);
    if (scene) {
        scene->tick(now, delta, wallClock());
        quality.record(now);
        quality.evaluate(now);
    }
    if (gameplay->leaveRequested && !confirmingLeave) confirmingLeave = true;
    if (gameplay->chatFocused != chatWasFocused) {
        chatWasFocused = gameplay->chatFocused;
        if (chatWasFocused && focusField) focusField("player.chatField");
        if (!chatWasFocused && focusViewport) focusViewport();
    }
    std::string after = std::to_string(gameplay->health) + "|" + std::to_string(gameplay->dead) + "|" + std::to_string(gameplay->menuOpen) + "|" + std::to_string(gameplay->chatFocused) + "|" + std::to_string(gameplay->chatOpen) + "|" + std::to_string(gameplay->console.size()) + "|" + std::to_string(gameplay->tools.size()) + "|" +
                        std::to_string(gameplay->loaded) + "|" + std::to_string(gameplay->leaveRequested);
    if (before != after) invalidate();
}

std::vector<ld::render::View> PlayerShell::views(const std::vector<ViewportSlot>& slots) {
    std::vector<ld::render::View> out;
    if (!scene) return out;
    for (const auto& slot : slots) {
        if (slot.id != "player.world") continue;
        ld::render::View v;
        v.key = slot.id;
        v.scene = &scene->scene();
        v.camera = &scene->camera();
        v.x = slot.frame.x;
        v.y = slot.frame.y;
        v.width = slot.frame.width;
        v.height = slot.frame.height;
        v.quality = quality.settings(slot.frame.width, slot.frame.height);
        out.push_back(v);
    }
    return out;
}

bool PlayerShell::pointer(const std::string& slot, const PointerEvent& event) {
    if (slot != "player.world" || !scene) return false;
    switch (event.phase) {
    case PointerPhase::down:
        last = event.location;
        if (focusViewport) focusViewport();
        scene->activateTool();
        return true;
    case PointerPhase::rightDown:
        last = event.location;
        if (focusViewport) focusViewport();
        return true;
    case PointerPhase::dragged:
    case PointerPhase::rightDragged:
        scene->look(event.location.x - last.x, -(event.location.y - last.y));
        last = event.location;
        return true;
    case PointerPhase::scroll: scene->zoom(event.scrollY, event.precise); return true;
    default: return false;
    }
}

bool PlayerShell::key(const KeyInput& key) {
    if (!scene || key.command) return false;
    scene->key(static_cast<std::uint16_t>(key.keyCode), key.down, key.repeat);
    return true;
}

void PlayerShell::resigned() {
    if (scene) scene->release();
}

NodePtr PlayerShell::build() {
    begin();
    std::vector<NodePtr> layers{rectangle(Color::black())};
    if (session) layers.push_back(stage());
    return zstack(std::move(layers))->id("player.shell");
}

NodePtr PlayerShell::loading(const std::optional<std::string>& message) {
    static const Image cover = loadImage(theme::resources() / "Backgrounds/Loading.jpg");
    std::string id = session->ticket.experienceId.text();
    std::optional<ld::ExperienceEntry> entry = store->join->experience();
    if (!entry)
        for (const auto& candidate : store->experienceCatalog())
            if (candidate.id == id) entry = candidate;
    std::string title = entry ? entry->title : session->world() ? session->world()->title : std::string("Ludissey");
    std::string author = entry ? entry->author : std::string();
    Image badge = experienceIcon ? experienceIcon(id) : Image{};
    NodePtr mark = appIcon(badge, entry ? entry->genre : std::string(), tokens::hud::loadingIcon);
    Font heading{std::string(tokens::font::family), tokens::font::loadingTitle, Weight::bold};
    std::vector<NodePtr> identity{embossed(lineLimited(text(title, heading, white()), 1), emboss::black)};
    if (!author.empty()) identity.push_back(lineLimited(text(tr("di {0}", author), type::body, white(tokens::opacity::secondaryValue)), 1));
    auto darkButton = [](const std::string& key, const std::string& label, std::function<void()> action) { return button(key, label, std::move(action), ButtonRole::dark); };
    Font state{std::string(tokens::font::family), tokens::font::status, Weight::regular};
    NodePtr status;
    double progress = 0;
    if (auto failure = session->failure()) {
        std::vector<NodePtr> actions{darkButton("player.leave", tr("Torna a Ludissey"), [this] { leave(); })};
        auto next = ld::failureAction(*failure);
        auto join = store->join;
        auto origin = join->experience() ? join->experience() : store->detail;
        if (next == ld::FailureAction::play && origin)
            actions.push_back(button("player.play", tr("Gioca"), [this, join, entry = *origin] {
                leave();
                join->cancel();
                store->play(entry);
            }, ButtonRole::primary));
        else if (next == ld::FailureAction::retry)
            actions.push_back(button("player.retry", tr("Riprova"), [this, join] {
                leave();
                join->retry();
            }, ButtonRole::primary));
        status = hstack({vstack({hstack({ui::icon("ld-icon-status-warning", iconSize::large), embossed(text(ld::failureTitle(*failure), type::strong, white()), emboss::black)}, spacing::s3), text(ld::failureMessage(*failure), type::small, white(0.9))->fixed(false, true)}, spacing::s1, HAlign::leading),
                         hstack(std::move(actions), spacing::s4)},
                        spacing::s8);
    } else {
        auto phase = session->phase();
        const std::array<ld::JoinPhase, 7> steps{ld::JoinPhase::resolvingExperience, ld::JoinPhase::requestingSession, ld::JoinPhase::launchingPlayer, ld::JoinPhase::connecting, ld::JoinPhase::loadingWorld, ld::JoinPhase::loadingAssets, ld::JoinPhase::enteringExperience};
        auto found = std::find(steps.begin(), steps.end(), phase);
        progress = phase == ld::JoinPhase::playing ? 1.0 : found == steps.end() ? 0.0 : static_cast<double>(found - steps.begin()) / static_cast<double>(steps.size());
        auto words = hstack({text(message.value_or(ld::phaseMessage(phase)), state, white(0.9)), text(tr("Server {0}", session->ticket.serverSequence), type::caption, white(0.6))}, spacing::s3);
        status = hstack({vstack({words, progressBar(progress)->frameSize(tokens::hud::loadingProgressWidth, std::nullopt)}, spacing::s3, HAlign::leading), darkButton("player.cancel", tr("Annulla"), [this] { leave(); })}, spacing::s8);
    }
    auto content = hstack({mark, vstack(std::move(identity), spacing::s1, HAlign::leading)->fixed(false, true), spacer(spacing::s8), status}, spacing::s8)->paddingHorizontal(spacing::s12)->flexible(std::nullopt, metrics::content, std::nullopt, std::nullopt);
    auto band = zstack({blackBar(), hstack({spacer(0.0), content, spacer(0.0)}, 0)})->frameSize(std::nullopt, tokens::hud::loadingBand)->id("player.loading.band");
    auto top = zstack({gradient({Color::black(shadows::loadingTop), Color::clear(), true}), hstack({logo(tokens::hud::statusLogo)->foreground(white()), spacer(0.0)}, 0)->padding(spacing::s12)}, Alignment::topLeading())->frameSize(std::nullopt, tokens::hud::loadingFade);
    std::vector<NodePtr> layers{rectangle(Color::black())};
    if (cover.valid()) layers.push_back(ui::picture(cover, true));
    layers.push_back(vstack({top, spacer(), band}, 0));
    return zstack(std::move(layers))->fill()->id("player.status");
}

NodePtr PlayerShell::healthBar() {
    double fraction = std::clamp(gameplay->health / ld::PlayerGameplay::maximumHealth, 0.0, 1.0);
    bool strong = gameplay->health > 30;
    auto bar = geometry([fraction, strong](Size size) { return zstack({rectangle(colors::hudTrack), rectangle(strong ? colors::success : colors::danger)->frameSize(size.width * fraction, std::nullopt)}, Alignment::leading()); })->clipped(metrics::smallRadius)->overlay(strokeRectangle(panel(), borders::pixels, metrics::smallRadius));
    bar->label = tr("Salute {0}", static_cast<int>(gameplay->health));
    return bar;
}

NodePtr PlayerShell::topbar(const std::string& title, const std::string& username) {
    auto menuToggle = plainButton("player.menuButton", logo(tokens::hud::logo)->foreground(white())->frameSize(tokens::hud::button + spacing::s3, tokens::hud::button), [this] {
                          gameplay->menuOpen = !gameplay->menuOpen;
                          invalidate();
                      })->help(tr("Menu di gioco (Esc)"));
    std::size_t unread = 0;
    for (std::size_t i = chatSeen; i < session->chat().size(); ++i)
        if (!muted.count(session->chat()[i].name)) ++unread;
    std::vector<NodePtr> chatLayers{ui::icon("ld-icon-app-chat", iconSize::bar)->frameSize(tokens::hud::button, tokens::hud::button)->background(glossBackground(ButtonRole::dark, metrics::barRadius, gameplay->chatOpen))};
    if (!gameplay->chatOpen && unread > 0) chatLayers.push_back(counter(static_cast<int>(unread))->offset(spacing::s4, -spacing::s3));
    auto chatButton = plainButton("player.chatButton", zstack(std::move(chatLayers), Alignment::topTrailing()), [this] {
                          gameplay->chatOpen = !gameplay->chatOpen;
                          gameplay->chatFocused = gameplay->chatOpen;
                          invalidate();
                      })->help(gameplay->chatOpen ? tr("Chat: chiudi") : tr("Chat: apri (/)"));
    chatButton->label = tr("Chat");
    bool chatVisible = ld::local_preferences::number(ld::local_preferences::keys::chatVisible).value_or(1) != 0;
    return hstack({menuToggle, chatVisible ? chatButton : spacer(0.0), lineLimited(text(title, type::small, white(0.75)), 1), spacer(spacing::s4), lineLimited(text(username, type::smallStrong, white()), 1), healthBar()->frameSize(tokens::hud::health, tokens::hud::healthHeight)->padding(Insets{0, 0, 0, spacing::s6})}, spacing::compact)
        ->frameSize(std::nullopt, tokens::hud::bar)
        ->fillWidth()
        ->background(blackBar())
        ->id("player.topbar");
}

NodePtr PlayerShell::hotbar() {
    std::vector<NodePtr> items;
    for (std::size_t i = 0; i < std::min<std::size_t>(9, gameplay->tools.size()); ++i) {
        const auto& tool = gameplay->tools[i];
        auto content = vstack({text(std::to_string(i + 1), type::micro, white(0.8))->fillWidth(Alignment::leading()), lineLimited(text(tool.name, type::smallStrong, white()), 2)->multilineAlignment(TextAlign::center), spacer(0.0)}, spacing::s1)
                           ->padding(spacing::tight)
                           ->frameSize(tokens::hud::slot, tokens::hud::slot)
                           ->background(rectangle(panel()))
                           ->clipped(metrics::cardRadius)
                           ->overlay(strokeRectangle(tool.equipped ? white() : white(0.25), tool.equipped ? borders::focus : borders::pixels, metrics::cardRadius));
        items.push_back(plainButton("player.hotbar." + std::to_string(i + 1), content, [this, i] {
            if (gameplay->equip) gameplay->equip(static_cast<int>(i));
        }));
    }
    return hstack(std::move(items), spacing::compact);
}

NodePtr PlayerShell::roster() {
    std::vector<NodePtr> header{ui::icon("ld-icon-app-players", iconSize::compact), text(tr("Giocatori"), type::smallStrong), spacer()};
    if (const auto& server = session->server()) header.push_back(text(std::to_string(server->players) + "/" + std::to_string(server->capacity), type::micro));
    std::vector<NodePtr> rows{hstack(std::move(header))->paddingHorizontal(spacing::inlineGap)->frameSize(std::nullopt, metrics::panelHeader)};
    std::string self = store->profile ? store->profile->username : "";
    for (const auto& name : session->roster()) {
        std::vector<NodePtr> cells{lineLimited(text(name, type::small, muted.count(name) ? white(0.5) : white()), 1), spacer(0.0)};
        if (name != self)
            cells.push_back(plainButton("player.mute." + name, text(muted.count(name) ? tr("Riattiva") : tr("Silenzia"), type::micro, white(0.8)), [this, name] {
                                if (!muted.erase(name)) muted.insert(name);
                                invalidate();
                            })->help(muted.count(name) ? tr("Mostra di nuovo i messaggi di {0}", name) : tr("Nascondi i messaggi di {0}", name)));
        rows.push_back(hstack(std::move(cells), spacing::s2)->paddingHorizontal(spacing::inlineGap)->flexible(std::nullopt, infinity, tokens::hud::rosterRow, std::nullopt, Alignment::leading()));
    }
    if (const auto& server = session->server())
        rows.push_back(text(tr("Server {0}", server->sequence), type::micro, white(0.7))->paddingHorizontal(spacing::inlineGap)->flexible(std::nullopt, infinity, tokens::hud::rosterRow, std::nullopt, Alignment::leading()));
    return vstack(std::move(rows), 0, HAlign::leading)->foreground(white())->frameSize(tokens::hud::roster, std::nullopt)->background(rectangle(panel()))->clipped(metrics::cardRadius)->id("player.list");
}

NodePtr PlayerShell::chat() {
    std::vector<NodePtr> lines;
    for (const auto& message : session->chat())
        if (!muted.count(message.name)) lines.push_back(styledText({{message.name + ": ", type::smallStrong, white()}, {message.text, type::small, white()}})->fillWidth(Alignment::leading()));
    Surface* surface = Surface::active();
    if (surface && session->chat().size() != chatCount) {
        chatCount = session->chat().size();
        surface->scrollTo("player.chat.messages", {0, 1e9});
    }
    auto history = scroll("player.chat.messages", vstack(std::move(lines), spacing::s1, HAlign::leading)->padding(spacing::compact))->frameSize(std::nullopt, tokens::hud::chatHistory);
    NativeField field;
    field.id = "player.chatField";
    field.prompt = gameplay->chatFocused ? tr("Scrivi un messaggio") : tr("Premi / per scrivere");
    field.text = draft;
    field.font = type::small;
    field.color = white();
    field.changed = [this](const std::string& value) { draft = value; };
    field.commit = [this](const std::string&) {
        std::string message = draft;
        draft.clear();
        gameplay->chatFocused = false;
        if (session) send(session, message).start();
        invalidate();
    };
    field.cancel = [this] {
        draft.clear();
        gameplay->chatFocused = false;
        invalidate();
    };
    field.focus = [this](bool focused) {
        if (!focused && gameplay->chatFocused) {
            gameplay->chatFocused = false;
            invalidate();
        }
    };
    auto input = textField(field)->paddingHorizontal(spacing::compact)->frameSize(std::nullopt, tokens::hud::chatField)->overlay(rectangle(white(0.2))->frameSize(std::nullopt, borders::pixels), Alignment::top());
    std::vector<NodePtr> parts{history, input};
    if (!session->chatError.empty()) parts.push_back(text(session->chatError, type::micro, white())->paddingHorizontal(spacing::compact)->background(rectangle(colors::danger))->id("player.chatError"));
    return vstack(std::move(parts), 0, HAlign::leading)->frameSize(tokens::hud::chat, std::nullopt)->background(rectangle(panel()))->clipped(metrics::cardRadius)->id("player.chat");
}

NodePtr PlayerShell::notice(const std::string& value) { return text(value, type::strong, white())->paddingHorizontal(spacing::s6)->paddingVertical(spacing::inlineGap)->background(rectangle(panel()))->clipped(metrics::cardRadius); }

NodePtr PlayerShell::menuButton(const std::string& id, const std::string& title, bool primary, std::function<void()> action) {
    auto label = scaledText(lineLimited(text(title, type::smallStrong, white()), 1), 0.75)->fillWidth();
    return button(id, label, [this, action] {
        action();
        invalidate();
    }, primary ? ButtonRole::primary : ButtonRole::dark)->fillWidth();
}

NodePtr PlayerShell::menu() {
    std::string serverLabel;
    if (const auto& server = session->server()) serverLabel = tr("Server {0}", server->sequence) + ", " + tr("{0} su {1}", server->players, server->capacity);
    std::string world = session->world() ? session->world()->title : std::string("Ludissey");
    std::vector<NodePtr> rows{hstack({embossed(lineLimited(text(confirmingLeave ? tr("Uscire da «{0}»?", world) : tr("Menu di gioco"), type::heading, white()), 1), emboss::black), spacer(), text(serverLabel, type::small, white(0.7))})->padding(spacing::s6)->background(blackBar())};
    if (confirmingLeave) {
        rows.push_back(hstack({menuButton("player.cancelLeave", tr("Resta"), false, [this] {
                                   confirmingLeave = false;
                                   gameplay->leaveRequested = false;
                               }),
                               menuButton("player.confirmLeave", tr("Esci"), true, [this] { leave(); })},
                              spacing::s6)
                           ->padding(spacing::s6));
    } else {
        std::vector<std::string> keys{"Giocatori", "Impostazioni", "Console"};
        int chosen = static_cast<int>(std::find(keys.begin(), keys.end(), gameplay->menuTab) - keys.begin());
        rows.push_back(hstack({spacer(0.0), segments("player.menu.tab", {tr("Giocatori"), tr("Impostazioni"), tr("Console")}, chosen, [this, keys](int i) {
                                   gameplay->menuTab = keys[static_cast<std::size_t>(i)];
                                   invalidate();
                               }, false, keys),
                               spacer(0.0)},
                              0)
                           ->paddingVertical(spacing::s4));
        NodePtr content;
        if (gameplay->menuTab == "Impostazioni") {
            content = vstack({text(tr("Sensibilità della camera: {0}", ld::localization::decimal(gameplay->sensitivity, 1)), type::small, white()), slider("player.sensitivity", gameplay->sensitivity, 0.25, 2.5, 0.25, colors::accent, [this](double value) {
                                                                                                                 gameplay->sensitivity = value;
                                                                                                                 invalidate();
                                                                                                             })},
                             spacing::compact, HAlign::leading);
        } else if (gameplay->menuTab == "Console") {
            std::vector<NodePtr> lines;
            if (gameplay->console.empty()) lines.push_back(text(tr("Nessun messaggio dagli script."), console, white(0.6)));
            std::size_t start = gameplay->console.size() > 10 ? gameplay->console.size() - 10 : 0;
            for (std::size_t i = start; i < gameplay->console.size(); ++i) lines.push_back(lineLimited(text(gameplay->console[i].stamped(), console, gameplay->console[i].error() ? colors::hudDanger : white()), 1));
            content = vstack(std::move(lines), spacing::s1, HAlign::leading)->id("player.console");
        } else {
            std::vector<NodePtr> names;
            for (const auto& name : session->roster()) names.push_back(text(name, type::small, white()));
            content = vstack(std::move(names), spacing::tight, HAlign::leading);
        }
        rows.push_back(content->flexible(std::nullopt, infinity, tokens::hud::menuBody, std::nullopt, Alignment::topLeading())->padding(spacing::s6));
        rows.push_back(hstack({menuButton("player.reset", tr("Reimposta personaggio (R)"), false, [this] { gameplay->reset(); }), menuButton("player.leave", tr("Esci dal gioco (L)"), false, [this] { confirmingLeave = true; }),
                               menuButton("player.resume", tr("Riprendi (Esc)"), true, [this] { gameplay->menuOpen = false; })},
                              spacing::s6)
                           ->padding(spacing::s6));
    }
    auto sheet = vstack(std::move(rows), 0)->frameSize(tokens::hud::menu, std::nullopt)->background(rectangle(panel()))->clipped(metrics::cardRadius)->overlay(strokeRectangle(white(0.25), borders::pixels, metrics::cardRadius));
    return zstack({rectangle(Color::black(shadows::scrim))->onTap("player.menu.scrim", [this] {
                       gameplay->menuOpen = false;
                       invalidate();
                   }),
                   sheet})
        ->id("player.menu");
}

NodePtr PlayerShell::stage() {
    prepareScene();
    auto look = store->currentAvatar();
    if (!(session->world() && look && session->ready())) return loading(std::nullopt);
    std::vector<NodePtr> layers{viewport("player.world")};
    GuiActions actions{gameplay->activate, gameplay->commit, gameplay->scroll};
    layers.push_back(overlay.build(gameplay->gui, actions, !gameplay->menuOpen)->padding(Insets{tokens::hud::bar, 0, 0, 0})->id("player.gui"));
    if (!gameplay->tools.empty()) layers.push_back(vstack({spacer(), hotbar()->padding(Insets{0, 0, spacing::s6, 0})}));
    if (!gameplay->loaded) layers.push_back(loading(tr("Carico il mondo…"))->background(rectangle(Color::black())));
    layers.push_back(topbar(session->world()->title, look->displayName));
    if (gameplay->chatOpen) chatSeen = session->chat().size();
    if (ld::local_preferences::number(ld::local_preferences::keys::chatVisible).value_or(1) == 0) {
        gameplay->chatOpen = false;
        gameplay->chatFocused = false;
    }
    layers.push_back(hstack({gameplay->chatOpen ? chat() : spacer(0.0), spacer(0.0), roster()}, 0, VAlign::top)->padding(Insets{tokens::hud::bar + spacing::s3, spacing::inlineGap, 0, spacing::inlineGap}));
    if (gameplay->dead) layers.push_back(notice(tr("Sei stato eliminato! Torni in gioco tra poco…"))->padding(Insets{tokens::hud::notice, 0, 0, 0}));
    if (gameplay->menuOpen) layers.push_back(menu());
    return zstack(std::move(layers), Alignment::top());
}

}
