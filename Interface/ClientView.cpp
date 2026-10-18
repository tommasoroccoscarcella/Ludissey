#include "ClientView.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/LocalPreferences.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Rendering/Quality.hpp"
#include "Interface/Surface.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

namespace ui {

namespace {

using namespace theme;
using ld::tr;
using ld::trn;

double surfaceWidth() {
    Surface* surface = Surface::active();
    return surface && surface->size().width > 0 ? surface->size().width : metrics::content;
}

double contentWidth(double width) { return std::min(width - 2 * spacing::s12, metrics::content); }

double tileWidth(double width) { return width >= breakpoints::wide ? metrics::tileStandard : metrics::tileCompact; }

int tilesPerRow(double width) { return std::max(1, static_cast<int>(std::floor((contentWidth(width) + spacing::s8) / (tileWidth(width) + spacing::s8)))); }

Font sized(double size, Weight weight) { return {std::string(tokens::font::family), size, weight}; }

NodePtr pageTitle(const std::string& title) { return embossed(text(title, type::pageTitle), emboss::light)->fillWidth(Alignment::leading())->padding(Insets{0, 0, spacing::s6, 0}); }

NodePtr sectionTitle(const std::string& value) { return embossed(text(value, type::sectionTitle, colors::textSecondary), emboss::light)->padding(Insets{spacing::s8, 0, spacing::s4, 0}); }

NodePtr presenceDot(bool online) { return presence(online, metrics::presence); }

NodePtr centered(NodePtr node) { return hstack({spacer(0.0), node, spacer(0.0)}, 0); }

std::string formatDate(const std::optional<std::string>& raw) {
    if (!raw || raw->empty()) return "—";
    const std::string& s = *raw;
    std::tm parts{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) return ld::localization::date(s);
    std::size_t zone = s.find_first_of("Z+-", 19);
    if (zone == std::string::npos) return ld::localization::date(s);
    long offset = 0;
    if (s[zone] != 'Z') {
        int h = 0, m = 0;
        if (std::sscanf(s.c_str() + zone + 1, "%2d:%2d", &h, &m) < 1) return ld::localization::date(s);
        offset = (h * 3600 + m * 60) * (s[zone] == '-' ? -1 : 1);
    }
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    std::time_t moment = timegm(&parts) - offset;
    std::tm local{};
    localtime_r(&moment, &local);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return ld::localization::date(text);
}

std::string shelfTitle(Shelf kind) {
    switch (kind) {
    case Shelf::featured: return tr("In evidenza");
    case Shelf::popular: return tr("Popolari");
    case Shelf::recent: return tr("Aggiornate di recente");
    case Shelf::community: return tr("Dalla community");
    case Shelf::mine: return tr("Le tue esperienze");
    }
    return "";
}

std::string_view shelfName(Shelf kind) {
    switch (kind) {
    case Shelf::featured: return "featured";
    case Shelf::popular: return "popular";
    case Shelf::recent: return "recent";
    case Shelf::community: return "community";
    case Shelf::mine: return "mine";
    }
    return "";
}

}

ClientView::ClientView(std::shared_ptr<ld::Store> value) : store(std::move(value)) {}

void ClientView::invalidate() {
    if (invalidated) invalidated();
}

void ClientView::leave(std::function<void()> action) {
    accountMenu = false;
    searchOpen = false;
    bool editing = store->route == ld::Route::avatar && !store->detail && store->avatarDraft != store->avatar;
    if (!editing) {
        action();
        invalidate();
        return;
    }
    auto owner = store;
    store->dialog = ld::DialogRequest{tr("Hai modifiche non salvate. Uscire lo stesso?"), "", {{tr("Resta"), ld::DialogRole::secondary, [] {}}, {tr("Esci"), ld::DialogRole::destructive, [owner, action] {
                                                                                                                                                 owner->revertAvatar();
                                                                                                                                                 action();
                                                                                                                                             }}}};
    invalidate();
}

NodePtr ClientView::field(const std::string& id, const std::string& prompt, std::string& value, bool secure, std::function<void()> commit, bool bare, bool multiline) {
    NativeField spec;
    spec.id = id;
    spec.prompt = prompt;
    spec.multiline = multiline;
    spec.text = value;
    spec.secure = secure;
    spec.changed = [this, target = &value](const std::string& next) {
        *target = next;
        if (!store->profile && store->error) store->error.reset();
        invalidate();
    };
    spec.commit = [commit](const std::string&) {
        if (commit) commit();
    };
    if (!bare) return styledField(std::move(spec));
    spec.font = type::body;
    spec.color = colors::textPrimary;
    return textField(std::move(spec));
}

NodePtr ClientView::build() {
    bool loggedIn = store->profile.has_value();
    if (loggedIn != authenticated) {
        authenticated = loggedIn;
        loginPassword.clear();
        password.clear();
        confirmation.clear();
        errorField.clear();
        bioLoaded = false;
        accountMenu = false;
        searchOpen = false;
        recovering = false;
        friendName.clear();
        friendError.clear();
        genre.reset();
        friendsTab = 0;
        settingsTab = 0;
        avatarTab = 0;
    }
    NodePtr content;
    if (store->restoringSession) content = loading(tr("Ripristino sessione…"))->id("session.restoring");
    else if (loggedIn) {
        NodePtr shell;
        if (ld::ludissey_target::isStudio() && studioShell) shell = studioShell();
        else if (ld::LaunchRequest::shared().joinTicket && playerShell) shell = playerShell();
        else shell = client();
        content = shell;
    } else content = login();
    std::vector<NodePtr> layers;
    if (store->restoringSession) layers.push_back(stripes());
    bool blocked = loggedIn && store->serverMode.blocking();
    layers.push_back(content->disabled((loggedIn && store->error.has_value()) || store->dialog.has_value() || blocked));
    if (blocked) {
        bool outdated = store->serverMode.kind == ld::BackendMode::Kind::outdated;
        std::vector<DialogButton> actions;
        if (outdated) {
            actions.push_back({tr("Esci"), ButtonRole::secondary, [this] {
                                   if (store->quit) store->quit();
                               }, "server.quit"});
            actions.push_back({tr("Scarica"), ButtonRole::primary, [this] { store->openDownloads(); }, "server.download"});
        } else actions.push_back({tr("Riprova"), ButtonRole::primary, [this] {
                                      store->reconnect();
                                      invalidate();
                                  }, "server.retry"});
        layers.push_back(dialog(outdated ? tr("Aggiorna Ludissey") : tr("Ludissey non è raggiungibile"), outdated ? tr("C’è una nuova versione. Scaricala da ludissey.com per continuare.") : tr("Controlla la connessione o riprova tra qualche minuto."), std::move(actions), true)->id("server.blocked"));
    } else if (store->error && loggedIn) {
        std::string title = tr("Operazione non riuscita"), message = *store->error;
        std::size_t cut = message.find(". ");
        if (cut != std::string::npos && cut + 2 < message.size()) {
            title = message.substr(0, cut);
            message = message.substr(cut + 2);
        }
        layers.push_back(dialog(title, message, {{tr("Chiudi"), ButtonRole::secondary, [this] {
                                                      store->error.reset();
                                                      invalidate();
                                                  }},
                                                 {tr("Riprova"), ButtonRole::primary, [this] {
                                                      store->error.reset();
                                                      store->refresh().start();
                                                      invalidate();
                                                  }}},
                                true));
    }
    if (store->dialog) {
        std::vector<DialogButton> actions;
        for (const auto& b : store->dialog->buttons)
            actions.push_back({b.label, b.role == ld::DialogRole::primary ? ButtonRole::primary : b.role == ld::DialogRole::destructive ? ButtonRole::destructive : ButtonRole::secondary, [this, action = b.action] {
                               store->dialog.reset();
                               if (action) action();
                               invalidate();
                           }});
        layers.push_back(dialog(store->dialog->title, store->dialog->message, std::move(actions), store->dialog->warning));
    }
    return zstack(std::move(layers))->font(type::body)->foreground(colors::textPrimary);
}

NodePtr ClientView::fieldError(const std::string& name) {
    if (!store->error || errorField != name) return empty();
    return hstack({icon("ld-icon-status-warning", iconSize::compact), text(*store->error, type::small, colors::danger)->fixed(false, true)}, spacing::s2, VAlign::top)->fillWidth(Alignment::leading())->id("login.error");
}

void ClientView::fail(const std::string& field, const std::string& message) {
    store->error = message;
    if (focusField) focusField(field);
    invalidate();
}

NodePtr ClientView::loginBar(double width) {
    auto submit = [this] {
        if (store->busy) return;
        errorField = "login";
        std::size_t length = ld::utf8Length(ld::trim(loginName));
        if (length < 3 || length > 20) return fail("login.username", tr("Il nome utente deve avere da 3 a 20 caratteri."));
        if (ld::utf8Length(loginPassword) < 6) return fail("login.password", tr("La password deve avere almeno 6 caratteri."));
        store->authenticate(loginName, loginPassword, false, gender);
        invalidate();
    };
    std::vector<NodePtr> items{embossed(logo(metrics::headerLogo + spacing::s3)->foreground(colors::headerText), emboss::black)->padding(Insets{spacing::s6, 0, 0, 0}), spacer(spacing::s6)};
    if (!store->needsProfile) {
        auto forgot = plainButton("login.forgot", text(tr("Password dimenticata?"), type::caption, colors::headerText), [this] {
                          recovering = true;
                          store->notice.clear();
                          store->error.reset();
                          invalidate();
                      }, Cursor::pointer)->disabled(store->busy);
        double side = width < breakpoints::narrow ? metrics::loginField - spacing::s12 : metrics::loginField;
        auto framed = [side](NodePtr node) { return node->frameSize(side, std::nullopt)->overlay(strokeRectangle(Color::black(), borders::pixels, metrics::fieldRadius)); };
        auto secret = vstack({framed(field("login.password", tr("Password"), loginPassword, true, submit)), forgot}, spacing::s2, HAlign::trailing);
        auto name = framed(field("login.username", tr("Nome utente"), loginName, false, submit));
        auto enter = button("login.submit", tr("Accedi"), submit, ButtonRole::primary);
        items.push_back(hstack({name, secret, enter}, spacing::s4, VAlign::top)->padding(Insets{spacing::s4, 0, 0, 0}));
    }
    return hstack(std::move(items), 0, VAlign::top)->paddingHorizontal(spacing::s12)->frameSize(std::nullopt, metrics::loginBar, Alignment::top())->fillWidth()->background(blackBar())->id("login.bar");
}

NodePtr ClientView::characterChoice() {
    static const Image male = loadImage(theme::resources() / "Faces/Male.png"), female = loadImage(theme::resources() / "Faces/Female.png");
    Color skin = Color::hex(std::string(ld::rules::avatar::standard::bodyColors::head));
    std::vector<NodePtr> cards;
    for (int i = 0; i < 2; ++i) {
        bool on = (gender == "male") == (i == 0);
        std::string id = "register.gender." + std::to_string(i);
        const Image& face = i == 0 ? male : female;
        std::vector<NodePtr> layers{rectangle(skin)};
        if (face.valid()) layers.push_back(picture(face, false));
        auto portrait = zstack(std::move(layers))->frameSize(metrics::face, metrics::face)->clipped(metrics::bustRadius)->overlay(strokeRectangle(colors::bustEdge, borders::pixels, metrics::bustRadius));
        NodePtr tile = vstack({portrait, text(i == 0 ? tr("Maschile") : tr("Femminile"), type::strong)}, spacing::s3)
                           ->padding(spacing::s4)
                           ->fillWidth()
                           ->background(rectangle(on ? colors::accentSurface : colors::surface))
                           ->clipped(metrics::groupRadius)
                           ->overlay(strokeRectangle(on ? colors::accent : colors::border, on ? borders::focus : borders::pixels, metrics::groupRadius));
        if (on) tile = zstack({tile, checkBadge(metrics::badge)->offset(spacing::s3, -spacing::s3)}, Alignment::topTrailing());
        auto item = plainButton(id, tile, [this, i] {
            gender = i == 0 ? "male" : "female";
            invalidate();
        }, Cursor::pointer);
        item->selected = on;
        item->label = i == 0 ? tr("Maschile") : tr("Femminile");
        cards.push_back(item);
    }
    return vstack({text(tr("Scegli il personaggio"), type::label, colors::textSecondary), hstack(std::move(cards), spacing::s6)}, spacing::s3, HAlign::leading);
}

NodePtr ClientView::loginPanel() {
    std::vector<NodePtr> lines;
    auto title = [](const std::string& value) { return text(value, sized(tokens::font::loginTitle, Weight::bold))->fixed(false, true); };
    auto wide = [](const std::string& value) { return hstack({spacer(), text(value), spacer()}, 0); };
    auto back = [](const std::string& id, std::function<void()> action) { return plainButton(id, text(tr("Torna indietro"), type::small, colors::link), std::move(action), Cursor::pointer); };
    auto entry = [this](const std::string& id, const std::string& label, const std::string& prompt, std::string& value, bool secure, std::function<void()> commit) {
        return hstack({text(label, type::label)->frameSize(metrics::loginLabel, std::nullopt, Alignment::leading()), field(id, prompt, value, secure, std::move(commit), true)->fillWidth()}, spacing::s4)->paddingHorizontal(spacing::s6)->frameSize(std::nullopt, metrics::listRow);
    };
    if (store->needsProfile) {
        lines.push_back(title(tr("Ultimo passo: scegli il tuo personaggio")));
        lines.push_back(characterChoice());
        if (store->error && errorField == "profile") lines.push_back(fieldError("profile"));
        lines.push_back(button("login.finish", wide(tr("Entra in Ludissey")), [this] {
                                 errorField = "profile";
                                 store->finishProfile(gender);
                                 invalidate();
                        }, ButtonRole::success, ButtonSize::large)->disabled(store->busy));
        lines.push_back(back("login.back", [this] { store->logout(); })->disabled(store->busy));
    } else if (recovering) {
        auto send = [this] {
            if (store->busy) return;
            errorField = "recovery";
            std::string email = ld::trim(recoveryEmail);
            if (email.find('@') == std::string::npos || email.size() > 254) return fail("login.recovery.email", tr("Scrivi un’email valida."));
            store->passwordEmail(email);
            invalidate();
        };
        lines.push_back(title(tr("Recupera la password")));
        lines.push_back(text(tr("Scrivi l’email del tuo account: ti mandiamo un link per cambiare password."), type::small, colors::textSecondary)->fixed(false, true));
        lines.push_back(group({entry("login.recovery.email", tr("Email"), tr("nome@esempio.it"), recoveryEmail, false, send)}));
        if (store->error && errorField == "recovery") lines.push_back(fieldError("recovery"));
        lines.push_back(button("login.recovery.send", wide(tr("Invia il link")), send, ButtonRole::success, ButtonSize::large));
        if (!store->notice.empty()) lines.push_back(text(store->notice, type::small, colors::textSecondary)->fixed(false, true)->id("login.recovery.notice"));
        lines.push_back(back("login.recovery.back", [this] {
                            recovering = false;
                            store->notice.clear();
                            store->error.reset();
                            invalidate();
                        })->disabled(store->busy));
    } else {
        auto submit = [this] {
            if (store->busy) return;
            std::size_t nameLength = ld::utf8Length(ld::trim(username));
            if (nameLength < 3 || nameLength > 20) {
                errorField = "register.username";
                return fail("register.username", tr("Il nome utente deve avere da 3 a 20 caratteri."));
            }
            if (ld::utf8Length(password) < 6) {
                errorField = "register.password";
                return fail("register.password", tr("La password deve avere almeno 6 caratteri."));
            }
            if (password != confirmation) {
                errorField = "register.confirm";
                return fail("register.confirm", tr("Le password non coincidono"));
            }
            errorField = "register.username";
            store->authenticate(username, password, true, gender);
            invalidate();
        };
        lines.push_back(title(tr("Registrati e gioca")));
        lines.push_back(group({entry("register.username", tr("Nome"), tr("Nome utente"), username, false, submit), entry("register.password", tr("Password"), tr("Almeno 6 caratteri"), password, true, submit),
                               entry("register.confirm", tr("Conferma"), tr("Ripeti la password"), confirmation, true, submit)}));
        for (const char* name : {"register.username", "register.password", "register.confirm"})
            if (store->error && errorField == name) lines.push_back(fieldError(name));
        lines.push_back(characterChoice());
        lines.push_back(button("register.submit", wide(tr("Registrati")), submit, ButtonRole::success, ButtonSize::large));
    }
    if (store->busy) lines.push_back(loading());
    return vstack(std::move(lines), spacing::s6, HAlign::leading)
        ->padding(spacing::s12)
        ->frameSize(metrics::loginWidth, std::nullopt)
        ->background(rectangle(colors::surface.opacity(shadows::loginPanel)))
        ->clipped(metrics::registrationRadius)
        ->boxShadow(drops::alert.color, 0, drops::alert.dy, drops::alert.blur, metrics::registrationRadius)
        ->id("login.card");
}

NodePtr ClientView::login() {
    bool studio = ld::ludissey_target::isStudio();
    static const Image city = loadImage(theme::resources() / "Backgrounds/Login.jpg");
    auto backdrop = canvas([this](Canvas& c, const Frame& f) {
        int width = static_cast<int>(std::ceil(f.width)), height = static_cast<int>(std::ceil(f.height));
        int wide = static_cast<int>(std::lround(width * c.scale())), high = static_cast<int>(std::lround(height * c.scale()));
        auto cached = std::find_if(loginBackdrops.begin(), loginBackdrops.end(), [&](const Image& i) { return i.pixelsWide == wide && i.pixelsHigh == high; });
        if (cached == loginBackdrops.end()) {
            Canvas scene(width, height, c.scale());
            Frame whole{0, 0, static_cast<double>(width), static_cast<double>(height)};
            if (city.valid()) {
                double w = city.pixelsWide, h = city.pixelsHigh, fit = std::max(whole.width / w, whole.height / h);
                scene.image(city, {whole.midX() - w * fit / 2, whole.midY() - h * fit / 2, w * fit, h * fit});
            } else scene.fill(whole, colors::header);
            scene.fillGradient({0, 0, whole.width * shadows::loginFadeReach, whole.height}, Gradient(Color::black(shadows::loginFade), Color::clear(), false));
            std::vector<std::uint8_t> rgba(scene.pixels(), scene.pixels() + static_cast<std::size_t>(scene.pixelWidth() * scene.pixelHeight() * 4));
            loginBackdrops.insert(loginBackdrops.begin(), imageFromRgba(rgba, scene.pixelWidth(), scene.pixelHeight(), true));
            if (loginBackdrops.size() > 4) loginBackdrops.pop_back();
            cached = loginBackdrops.begin();
        }
        c.pixelImage(*cached, {f.x, f.y, static_cast<double>(width), static_cast<double>(height)});
    });
    Color haze = Color::black(shadows::text);
    Font slogan = sized(tokens::font::slogan, Weight::bold);
    std::vector<NodePtr> mark{logo(metrics::loginLogo)->foreground(colors::headerText)->shadow(haze, 0, borders::focus, borders::focus)};
    if (studio) mark.push_back(text("Ludissey Studio", slogan, colors::headerText)->shadow(haze, 0, borders::focus, borders::focus));
    std::vector<NodePtr> pitch;
    for (const auto& line : studio ? std::vector<std::string>{tr("Costruisci, prova e pubblica i tuoi mondi.")} : std::vector<std::string>{tr("Gioca ai mondi della community."), tr("Poi costruisci il tuo.")})
        pitch.push_back(text(line, slogan, colors::headerText)->fixed(true, true)->shadow(haze, 0, borders::focus, borders::focus));
    auto brand = vstack({hstack(std::move(mark), spacing::s8), vstack(std::move(pitch), 0, HAlign::leading)}, spacing::s8, HAlign::leading)
                     ->fixed(true, true)
                     ->id("login.brand");
    return geometry([this, backdrop, brand](Size size) {
        std::vector<NodePtr> stack{loginBar(size.width)};
        if (store->error && errorField == "login")
            stack.push_back(hstack({spacer(0.0), hstack({icon("ld-icon-status-warning", iconSize::compact), text(*store->error, type::small, colors::headerText)}, spacing::s2)->paddingHorizontal(spacing::s6)->paddingVertical(spacing::s3)->background(blackBar())->id("login.error")}, 0)->paddingHorizontal(spacing::s12));
        bool roomy = size.width >= 2 * metrics::loginWidth + 2 * spacing::s12 + spacing::s16;
        NodePtr row = roomy ? hstack({brand, spacer(spacing::s8), loginPanel()}, 0) : hstack({spacer(0.0), loginPanel(), spacer(0.0)}, 0);
        stack.push_back(row->paddingHorizontal(spacing::s12)->flexible(std::nullopt, metrics::content, std::nullopt, std::nullopt)->fill());
        return zstack({backdrop, vstack(std::move(stack), 0)});
    });
}

std::string ClientView::searchPrompt() const {
    if (store->detail) return tr("Cerca esperienze");
    switch (store->route) {
    case ld::Route::home:
    case ld::Route::experiences: return tr("Cerca esperienze");
    case ld::Route::friends: return tr("Cerca amici");
    case ld::Route::avatar: return tr("Cerca oggetti");
    case ld::Route::settings: return "";
    case ld::Route::profile: return tr("Cerca esperienze");
    }
    return "";
}

NodePtr ClientView::searchField(double width) {
    NativeField spec;
    spec.id = "utility.search";
    spec.prompt = searchPrompt();
    spec.text = store->searchQuery;
    spec.font = sized(tokens::font::button, Weight::regular);
    spec.color = colors::textPrimary;
    spec.changed = [this](const std::string& next) {
        if (store->detail || store->listing || store->profileId) store->navigate(ld::Route::experiences);
        store->searchQuery = next;
        invalidate();
    };
    std::vector<NodePtr> items{icon("ld-icon-app-search", iconSize::compact), textField(std::move(spec))};
    if (!store->searchQuery.empty()) items.push_back(plainButton("utility.search.clear", glyph("chiudi", iconSize::compact, colors::textSecondary), [this] {
                                                         store->searchQuery.clear();
                                                         invalidate();
                                                     })->help(tr("Cancella ricerca")));
    auto pill = canvas([](Canvas& c, const Frame& f) {
        double radius = f.height / 2;
        c.fill(f, colors::surface, radius);
        c.innerShadow(f, radius, drops::field.dy, drops::pressed.blur, Color::black(tokens::opacity::searchShade));
        c.stroke(f, colors::searchBorder, borders::pixels, radius);
    });
    return hstack(std::move(items), spacing::s3)->padding(Insets{0, spacing::s4, 0, spacing::s6})->frameSize(width, metrics::headerSearch)->background(pill)->help(searchPrompt())->id("utility.searchField");
}

NodePtr ClientView::navigationItem(const std::string& id, const std::string& title, bool selected, int count, std::function<void()> action) {
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered(id);
    std::vector<NodePtr> parts{embossed(lineLimited(text(title, sized(tokens::font::navigation, Weight::bold), colors::headerText), 1)->fixed(true, false), emboss::dark)};
    if (count > 0) parts.push_back(counter(count));
    NodePtr fill = selected ? recess() : hovered ? roundedRectangle(metrics::switchRadius, Color::white(shadows::utilityHover)) : rectangle(Color::clear());
    auto label = hstack(std::move(parts), spacing::s2)->paddingHorizontal(spacing::s6)->frameSize(std::nullopt, metrics::barButton)->background(fill);
    auto item = plainButton(id, label->paddingHorizontal(spacing::s1), [this, action] { leave(action); }, Cursor::pointer);
    item->selected = selected;
    item->label = title;
    return item;
}

NodePtr ClientView::utility(const std::string& id, const std::string& name, int count, const std::string& label, bool open, std::function<void()> action) {
    std::vector<NodePtr> layers{barButton(id, icon(name, iconSize::bar), [this, action] {
        action();
        invalidate();
    }, open)};
    if (count > 0) layers.push_back(counter(count)->offset(spacing::s4, -spacing::s4));
    auto result = zstack(std::move(layers), Alignment::topTrailing());
    result->label = label;
    return result->help(label)->padding(Insets{0, 0, 0, spacing::s3});
}

NodePtr ClientView::bust(const std::string& name, double side) {
    Image picture = portrait ? portrait(name) : Image{};
    if (!picture.valid()) {
        std::string gender = store->profile && store->profile->username == name ? store->profile->gender : "male";
        for (const auto& f : store->friends)
            if (f.username == name) gender = f.gender;
        static const Image male = loadImage(theme::resources() / "Portraits/Male.png"), female = loadImage(theme::resources() / "Portraits/Female.png");
        picture = gender == "female" ? female : male;
    }
    double radius = metrics::bustRadius;
    NodePtr face = picture.valid() ? ui::picture(picture, true) : rectangle(colors::surfaceMuted);
    return face->frameSize(side, side)->background(rectangle(colors::surfaceMuted))->clipped(radius)->overlay(strokeRectangle(colors::bustEdge, borders::pixels, radius));
}

NodePtr ClientView::navigation(double width) {
    bool plain = !store->detail;
    std::vector<NodePtr> items{plainButton("topNavigation.logo", embossed(logo(metrics::headerLogo)->foreground(colors::headerText), emboss::dark), [this] {
                                   leave([this] {
                                       store->navigate(ld::Route::home);
                                   });
                               }, Cursor::pointer)->padding(Insets{0, 0, 0, spacing::s6})};
    int requests = static_cast<int>(store->requests.size()), unread = store->unreadNotifications();
    for (auto route : {ld::Route::home, ld::Route::experiences, ld::Route::avatar, ld::Route::friends}) {
        bool selected = (plain && store->route == route) || (route == ld::Route::experiences && store->detail);
        items.push_back(navigationItem("topNavigation." + std::string(ld::routeName(route)), ld::routeTitle(route), selected, route == ld::Route::friends ? requests : 0, [this, route] {
            store->navigate(route);
        }));
    }
    items.push_back(navigationItem("topNavigation.create", tr("Crea"), false, 0, [this] { store->openStudio(); }));
    items.push_back(spacer(spacing::s4));
    if (!searchPrompt().empty()) {
        if (width >= breakpoints::narrow) items.push_back(searchField(width >= breakpoints::wide ? metrics::searchWide : metrics::searchNarrow)->padding(Insets{0, 0, 0, spacing::s4}));
        else items.push_back(utility("utility.searchToggle", "ld-icon-app-search", 0, searchPrompt(), searchOpen, [this] { searchOpen = !searchOpen; }));
    }
    items.push_back(utility("utility.notifications", "ld-icon-app-notifications", unread, unread ? trn("{0} notifica da leggere", "{0} notifiche da leggere", unread) : tr("Notifiche"), store->notificationsOpen, [this] {
        store->notificationsOpen = !store->notificationsOpen;
        accountMenu = false;
    }));
    items.push_back(utility("topNavigation.settings", "ld-icon-app-settings", 0, tr("Impostazioni"), accountMenu, [this] {
        accountMenu = !accountMenu;
        store->notificationsOpen = false;
    }));
    std::string name = store->profile ? store->profile->username : std::string();
    std::vector<NodePtr> identity{bust(name, metrics::headerAvatar)};
    if (width >= breakpoints::narrow) {
        if (ld::utf8Length(name) > 14) name = ld::utf8Prefix(name, 13) + "…";
        identity.insert(identity.begin(), embossed(lineLimited(text(name, sized(tokens::font::navigation, Weight::bold), colors::headerText), 1)->fixed(true, false), emboss::dark));
    }
    Surface* surface = Surface::active();
    bool hovered = surface && surface->hovered("topNavigation.profile");
    auto chip = plainButton("topNavigation.profile", hstack(std::move(identity), spacing::s4)->paddingHorizontal(spacing::s4)->frameSize(std::nullopt, metrics::barButton)->background(hovered ? roundedRectangle(metrics::switchRadius, Color::white(shadows::utilityHover)) : rectangle(Color::clear())), [this] { openOwnProfile(); }, Cursor::pointer);
    chip->label = store->profile ? store->profile->username : std::string();
    items.push_back(chip->help(tr("Il tuo profilo"))->padding(Insets{0, spacing::s2, 0, 0}));
    return hstack(std::move(items), 0)->paddingHorizontal(spacing::s8)->fill()->background(navigationBar());
}

NodePtr ClientView::client() {
    if (auto link = ld::LaunchRequest::shared().experienceLink) {
        ld::LaunchRequest::shared().experienceLink.reset();
        store->follow(*link).start();
    }
    double width = surfaceWidth();
    std::vector<NodePtr> column;
    if (!store->notice.empty())
        column.push_back(card(hstack({text(store->notice, type::body, colors::textPrimary)->fixed(false, true), spacer(spacing::s4), plainButton("notice.close", glyph("chiudi", iconSize::compact, colors::textSecondary), [this] {
                                                                                                                           store->notice.clear();
                                                                                                                           invalidate();
                                                                                                                       })->help(tr("Chiudi"))},
                                     spacing::s4)
                                  ->paddingHorizontal(spacing::s6)
                                  ->paddingVertical(spacing::s4)
                                  ->fillWidth(Alignment::leading()))
                             ->padding(Insets{0, 0, spacing::s6, 0})
                             ->id("notice"));
    column.push_back(page());
    auto body = vstack(std::move(column), 0, HAlign::leading)->padding(spacing::s12)->flexible(std::nullopt, metrics::content + 2 * spacing::s12, std::nullopt, std::nullopt, Alignment::topLeading())->fillWidth(Alignment::top());
    auto content = scroll("client.page", body)->background(stripes());
    std::vector<NodePtr> stack{geometry([this](Size size) { return navigation(size.width); })->frameSize(std::nullopt, metrics::navigationBar)->id("topNavigation")};
    if (searchOpen && width < breakpoints::narrow && !searchPrompt().empty()) stack.push_back(searchField(width - 2 * spacing::s8)->paddingHorizontal(spacing::s8)->frameSize(std::nullopt, metrics::navigationBar)->fillWidth()->background(navigationBar()));
    stack.push_back(content);
    std::vector<NodePtr> layers{vstack(std::move(stack), 0)};
    if (store->notificationsOpen || accountMenu) {
        layers.push_back(rectangle(Color::clear())->onTap("menu.dismiss", [this] {
            store->notificationsOpen = false;
            accountMenu = false;
            invalidate();
        }));
        layers.push_back((store->notificationsOpen ? notifications() : accountDropdown())->padding(Insets{0, 0, 0, spacing::s4})->offset(0, metrics::navigationBar - spacing::s2)->fill(Alignment::topTrailing()));
    }
    if (ld::phaseActive(store->join->phase()) || store->join->failure()) layers.push_back(joinOverlay());
    return zstack(std::move(layers), Alignment::topLeading());
}

NodePtr ClientView::accountDropdown() {
    auto rows = vstack({menuRow("account.menu.settings", text(tr("Impostazioni")), [this] {
                            accountMenu = false;
                            leave([this] {
                                store->navigate(ld::Route::settings);
                            });
                        }),
                        rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels),
                        menuRow("account.menu.logout", text(tr("Esci")), [this] {
                            accountMenu = false;
                            store->confirmLogout();
                            invalidate();
                        })},
                       0, HAlign::leading);
    return popover(rows, metrics::menu, "", nullptr, pointer("topNavigation.settings", metrics::menu))->id("account.menu");
}

NodePtr ClientView::notifications() {
    std::vector<NodePtr> rows;
    for (const auto& n : store->notifications) {
        std::string line = ld::localization::translateMessage(n.title);
        std::string detail = ld::localization::translateMessage(n.body);
        std::vector<NodePtr> words{text(line, n.unread ? type::strong : type::body)->fixed(false, true)};
        if (!detail.empty()) words.push_back(text(detail, type::small, colors::textSecondary)->fixed(false, true));
        if (!n.at.empty()) words.push_back(text(formatDate(n.at), type::caption, colors::textTertiary)->id("notification.time." + n.id));
        std::vector<NodePtr> items{icon("ld-icon-app-friends", iconSize::large), plainButton("notification." + n.id, vstack(std::move(words), spacing::s1, HAlign::leading), [this] {
                                       store->notificationsOpen = false;
                                       friendsTab = 1;
                                       store->navigate(ld::Route::friends);
                                       invalidate();
                                   }, Cursor::pointer),
                                   spacer(0.0)};
        if (n.unread) items.push_back(unreadDot(spacing::s6));
        items.push_back(plainButton("notification.dismiss." + n.id, glyph("chiudi", iconSize::compact, colors::textTertiary), [this, id = n.id] {
                            store->dismissNotification(id);
                            invalidate();
                        })->help(tr("Rimuovi notifica")));
        if (!rows.empty()) rows.push_back(rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels));
        rows.push_back(hstack(std::move(items), spacing::s4)->paddingHorizontal(spacing::s6)->paddingVertical(spacing::s4)->fillWidth(Alignment::leading()));
    }
    NodePtr body;
    if (store->notifications.empty()) body = hstack({icon("ld-icon-app-notifications", iconSize::large), text(tr("Nessuna notifica."), type::body, colors::textSecondary)}, spacing::s4)->padding(spacing::s6)->fillWidth(Alignment::leading())->id("notifications.empty");
    else body = scroll("notifications.list", vstack(std::move(rows), 0, HAlign::leading)->fillWidth(Alignment::leading()))->frameSize(std::nullopt, std::min(metrics::popoverWidth, static_cast<double>(store->notifications.size()) * (metrics::listRow * 1.6)));
    NodePtr action;
    if (store->unreadNotifications() > 0)
        action = barButton("notifications.readAll", text(tr("Segna come lette")), [this] {
            store->markNotificationsRead();
            invalidate();
        });
    return popover(body, metrics::popoverWidth, tr("Notifiche"), action, pointer("utility.notifications", metrics::popoverWidth))->id("notifications");
}

double ClientView::pointer(const std::string& anchor, double width) const {
    Surface* surface = Surface::active();
    auto found = surface ? surface->find(anchor) : std::nullopt;
    if (!found) return -spacing::s16;
    double right = surface->size().width - spacing::s4;
    return std::clamp(found->frame.midX() - (right - width), metrics::popoverRadius + metrics::popoverArrow, width - metrics::popoverRadius - metrics::popoverArrow);
}

std::string ClientView::backTitle() const {
    auto previous = store->previousPage();
    if (!previous) return ld::routeTitle(store->route);
    if (previous->profileId) return previous->profileName;
    if (previous->detail) return previous->detail->title;
    if (previous->listing) return shelfTitle(*previous->listing);
    return ld::routeTitle(previous->route);
}

NodePtr ClientView::page() {
    if (store->detail) return experience(*store->detail);
    switch (store->route) {
    case ld::Route::home: return home();
    case ld::Route::experiences: return experiences();
    case ld::Route::avatar: return avatar();
    case ld::Route::friends: return friends();
    case ld::Route::settings: return settings();
    case ld::Route::profile: return profilePage();
    }
    return empty();
}

NodePtr ClientView::cover(const ld::ExperienceEntry& entry, double width, double height) {
    Image picture = thumbnail ? thumbnail(entry.id) : Image{};
    if (picture.valid()) return zstack({rectangle(colors::surfaceAlt), ui::picture(picture, true)})->frameSize(width, height)->clipped();
    Color tint = genreColor(entry.genre);
    return canvas([tint](Canvas& c, const Frame& f) {
               c.fill(f, tint);
               double side = std::min(f.width, f.height) * 0.42;
               drawMark(c, {f.midX() - side / 2, f.midY() - side / 2, side, side}, colors::headerText);
           }, Size{width, height})
        ->frameSize(width, height)
        ->clipped();
}

Image ClientView::badge(const ld::ExperienceEntry& entry) {
    return experienceIcon ? experienceIcon(entry.id) : Image{};
}

int ClientView::playing(const ld::ExperienceEntry& entry) const {
    auto known = store->details.find(entry.id);
    return known == store->details.end() ? 0 : known->second.players();
}

NodePtr ClientView::experienceCard(const ld::ExperienceEntry& e, double side) {
    std::string id = "experience.tile." + e.id;
    int players = playing(e);
    std::string meta = !e.published ? tr("Bozza") : players > 0 ? tr("{0} in gioco", ld::localization::number(players)) : ld::genreTitle(e.genre);
    auto words = vstack({lineLimited(text(e.title, sized(tokens::font::tileTitle, Weight::bold)), 2)->multilineAlignment(TextAlign::center)->fixed(false, true), lineLimited(text(meta, type::caption, colors::textTertiary), 1)}, spacing::s1);
    auto face = vstack({appIcon(badge(e), e.genre, metrics::tileIcon), embossed(words, emboss::light)}, spacing::s3)->frameSize(side, std::nullopt, Alignment::top());
    return plainButton(id, face, [this, e] {
               store->showExperience(e);
               invalidate();
           }, Cursor::pointer)
        ->help(e.title);
}

NodePtr ClientView::experienceRow(const ld::ExperienceEntry& e) {
    int players = playing(e);
    std::vector<NodePtr> words{lineLimited(text(e.title, type::label), 1), lineLimited(text(e.author.empty() ? ld::genreTitle(e.genre) : tr("di {0}", e.author) + " · " + ld::genreTitle(e.genre), type::small, colors::textTertiary), 1)};
    if (players > 0) words.push_back(text(tr("{0} in gioco", ld::localization::number(players)), sized(tokens::font::small, Weight::bold), colors::success));
    auto open = plainButton("experience.row." + e.id, hstack({appIcon(badge(e), e.genre, metrics::listIcon), vstack(std::move(words), spacing::s1, HAlign::leading)}, spacing::s6), [this, e] {
                    store->showExperience(e);
                    invalidate();
                }, Cursor::pointer)
                    ->help(e.title);
    auto play = button("experience.row.play." + e.id, tr("Gioca"), [this, e] { store->play(e); }, ButtonRole::success, ButtonSize::small)->disabled(store->join->busy());
    return hstack({open, spacer(spacing::s4), play}, spacing::s4)->padding(spacing::s4)->fillWidth(Alignment::leading());
}

NodePtr ClientView::experienceList(const std::vector<ld::ExperienceEntry>& entries, double width) {
    if (entries.empty()) return emptyState("ld-icon-app-experiences", tr("Ancora nessuna esperienza qui."));
    bool split = width >= breakpoints::small && entries.size() > 1;
    std::size_t half = split ? (entries.size() + 1) / 2 : entries.size();
    std::vector<NodePtr> left, right;
    for (std::size_t i = 0; i < entries.size(); ++i) (i < half ? left : right).push_back(experienceRow(entries[i]));
    if (!split) return group(std::move(left));
    return hstack({group(std::move(left))->fillWidth(Alignment::top()), group(std::move(right))->fillWidth(Alignment::top())}, spacing::s6, VAlign::top);
}

std::vector<ld::ExperienceEntry> ClientView::shelfEntries(Shelf kind) const {
    std::vector<ld::ExperienceEntry> out;
    for (const auto& e : store->experienceCatalog()) {
        if (genre && *genre != e.genre) continue;
        bool keep = false;
        switch (kind) {
        case Shelf::featured: keep = e.categories.count(ld::ExperienceScope::featured) > 0; break;
        case Shelf::popular: keep = e.published; break;
        case Shelf::recent: keep = e.published && e.updatedAt.has_value(); break;
        case Shelf::community: keep = e.categories.count(ld::ExperienceScope::community) > 0; break;
        case Shelf::mine: keep = e.categories.count(ld::ExperienceScope::mine) > 0; break;
        }
        if (keep) out.push_back(e);
    }
    if (kind == Shelf::popular) std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.weeklyPlays.value_or(0) > b.weeklyPlays.value_or(0); });
    if (kind == Shelf::recent) std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.updatedAt.value_or("") > b.updatedAt.value_or(""); });
    return out;
}

NodePtr ClientView::shelf(Shelf kind, const std::string& title, double width) {
    auto entries = shelfEntries(kind);
    std::string name(shelfName(kind));
    std::vector<NodePtr> heading{embossed(text(title, type::sectionTitle, colors::textSecondary), emboss::light), spacer(0.0)};
    if (!entries.empty())
        heading.push_back(button("shelf." + name + ".all", tr("Vedi tutte"), [this, kind] {
            genre.reset();
            store->showListing(kind);
            invalidate();
        }, ButtonRole::primary, ButtonSize::small));
    auto header = hstack(std::move(heading), spacing::s4)->padding(Insets{spacing::s8, 0, spacing::s4, 0});
    NodePtr row;
    if (entries.empty()) {
        NodePtr action = kind == Shelf::mine ? button("shelf.mine.studio", tr("Apri Ludissey Studio"), [this] { store->openStudio(); }) : nullptr;
        row = emptyState(kind == Shelf::mine ? "ld-icon-app-create" : "ld-icon-app-experiences", kind == Shelf::mine ? tr("Non hai ancora creato niente.") : tr("Ancora nessuna esperienza qui."), action);
    } else {
        std::size_t count = std::min(entries.size(), static_cast<std::size_t>(tilesPerRow(width)));
        double side = tileWidth(width);
        std::vector<NodePtr> tiles;
        for (std::size_t i = 0; i < count; ++i) tiles.push_back(experienceCard(entries[i], side));
        tiles.push_back(spacer(0.0));
        row = hstack(std::move(tiles), spacing::s8, VAlign::top)->padding(Insets{spacing::s3, 0, 0, 0});
    }
    return vstack({header, row}, 0, HAlign::leading)->id("shelf." + name);
}

NodePtr ClientView::home() {
    double width = surfaceWidth();
    std::string name = store->profile ? store->profile->username : std::string();
    int online = static_cast<int>(std::count_if(store->friends.begin(), store->friends.end(), [](const auto& f) { return f.online; }));
    std::vector<NodePtr> greeting{embossed(lineLimited(text(tr("Ciao, {0}!", name), type::pageTitle), 1), emboss::light)};
    if (online > 0) greeting.push_back(embossed(text(trn("{0} amico è online adesso", "{0} amici sono online adesso", online), type::small, colors::textTertiary), emboss::light));
    auto headshot = plainButton("home.headshot", bust(name, metrics::headshot)->boxShadow(drops::appIcon.color, 0, drops::appIcon.dy, drops::appIcon.blur, metrics::bustRadius), [this] { openOwnProfile(); }, Cursor::pointer)->help(tr("Il tuo profilo"));
    std::vector<NodePtr> rows{hstack({headshot, vstack(std::move(greeting), spacing::s2, HAlign::leading)}, spacing::s8)->padding(Insets{0, 0, spacing::s4, 0})};
    if (!store->searchText().empty()) {
        std::vector<ld::ExperienceEntry> found;
        for (const auto& e : store->experienceCatalog())
            if (store->matchesSearch(e.title)) found.push_back(e);
        rows.push_back(sectionTitle(tr("Risultati per «{0}»", store->searchQuery)));
        rows.push_back(found.empty() ? emptyState("ld-icon-app-search", tr("Nessun risultato per «{0}».", store->searchQuery)) : experienceList(found, width));
        return vstack(std::move(rows), 0, HAlign::leading);
    }
    rows.push_back(hstack({embossed(text(tr("Amici ({0})", static_cast<long long>(store->friends.size())), type::sectionTitle, colors::textSecondary), emboss::light), spacer(0.0), button("home.friends.all", tr("Vedi tutti"), [this] {
                               friendsTab = 0;
                               store->navigate(ld::Route::friends);
                               invalidate();
                           }, ButtonRole::primary, ButtonSize::small)},
                          spacing::s4)
                       ->padding(Insets{spacing::s8, 0, spacing::s4, 0}));
    if (store->friends.empty()) rows.push_back(emptyState("ld-icon-app-friends", tr("Aggiungi amici dalla pagina Amici.")));
    else {
        std::vector<NodePtr> people;
        double cell = metrics::friendBust + spacing::s12;
        std::size_t limit = static_cast<std::size_t>(std::max(1.0, std::floor((contentWidth(width) - 2 * spacing::s6) / (cell + spacing::s6))));
        for (std::size_t i = 0; i < store->friends.size() && i < limit; ++i) {
            const auto& f = store->friends[i];
            people.push_back(plainButton("home.friend." + f.id, vstack({zstack({bust(f.username, metrics::friendBust), presenceDot(f.online)->offset(spacing::s2, spacing::s2)}, Alignment::bottomTrailing()), lineLimited(text(f.username, type::smallStrong), 1)->frameSize(cell, std::nullopt)}, spacing::s3), [this, id = f.id, name = f.username] { openPlayer(id, name); }, Cursor::pointer)->help(f.username));
        }
        people.push_back(spacer(0.0));
        rows.push_back(card(hstack(std::move(people), spacing::s6, VAlign::top)->padding(spacing::s6)->fillWidth(Alignment::leading()))->id("home.friends"));
    }
    rows.push_back(shelf(Shelf::popular, tr("Popolari"), width));
    rows.push_back(shelf(Shelf::recent, tr("Aggiornate di recente"), width));
    rows.push_back(shelf(Shelf::mine, tr("Le tue esperienze"), width));
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr ClientView::experiences() {
    double width = surfaceWidth();
    std::vector<NodePtr> rows;
    if (!store->searchText().empty()) {
        std::vector<ld::ExperienceEntry> found;
        for (const auto& e : store->experienceCatalog())
            if (store->matchesSearch(e.title)) found.push_back(e);
        rows.push_back(pageTitle(tr("Risultati per «{0}»", store->searchQuery)));
        rows.push_back(found.empty() ? emptyState("ld-icon-app-search", tr("Nessun risultato per «{0}».", store->searchQuery)) : experienceList(found, width));
        return vstack(std::move(rows), 0, HAlign::leading);
    }
    if (store->listing) {
        NativeField picker;
        picker.id = "experiences.genre";
        picker.options.push_back(tr("Tutti"));
        picker.selectedOption = 0;
        for (std::size_t i = 0; i < ld::rules::genres.size(); ++i) {
            std::string name(ld::rules::genres[i]);
            picker.options.push_back(ld::genreTitle(name));
            if (genre == name) picker.selectedOption = static_cast<int>(i) + 1;
        }
        picker.choose = [this](int index) {
            genre = index <= 0 ? std::nullopt : std::optional<std::string>(std::string(ld::rules::genres[static_cast<std::size_t>(index - 1)]));
            invalidate();
        };
        rows.push_back(backButton("experiences.back", backTitle(), [this] {
                           genre.reset();
                           store->goBack();
                           invalidate();
                       })->padding(Insets{0, 0, spacing::s8, 0}));
        rows.push_back(hstack({embossed(text(shelfTitle(*store->listing), type::pageTitle), emboss::light), spacer(0.0), embossed(text(tr("Genere"), type::small, colors::textTertiary), emboss::light), select(picker)->frameSize(metrics::menu, std::nullopt)}, spacing::s4)
                           ->padding(Insets{0, 0, spacing::s8, 0}));
        rows.push_back(experienceList(shelfEntries(*store->listing), width)->id("experiences.grid"));
        return vstack(std::move(rows), 0, HAlign::leading);
    }
    rows.push_back(pageTitle(tr("Esperienze")));
    for (auto kind : {Shelf::featured, Shelf::popular, Shelf::recent, Shelf::community}) rows.push_back(shelf(kind, shelfTitle(kind), width));
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr ClientView::facts(const ld::ExperienceEntry& e, const ld::ExperienceDetails* details) {
    const ld::ExperienceFacts* known = details ? &details->experience : nullptr;
    int size = known ? known->serverSize : e.serverSize;
    std::vector<std::pair<std::string, std::string>> cells{
        {tr("In gioco"), details ? ld::localization::number(details->players()) : "—"},
        {tr("Visite"), known ? ld::localization::number(known->visits) : "—"},
        {tr("Creata"), formatDate(known ? known->createdAt : e.createdAt)},
        {tr("Aggiornata"), formatDate(known ? known->updatedAt : e.updatedAt)},
        {tr("Giocatori per server"), size > 0 ? ld::localization::number(size) : "—"},
        {tr("Genere"), ld::genreTitle(known ? known->genre : e.genre)},
    };
    std::vector<NodePtr> items;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        auto cell = vstack({lineLimited(text(cells[i].first, type::small, colors::textTertiary), 1), lineLimited(text(cells[i].second, type::sectionTitle), 1)}, spacing::s1)->paddingVertical(spacing::s4)->paddingHorizontal(spacing::s2)->fillWidth()->id("experience.stat." + std::to_string(i));
        cell->label = cells[i].first + " " + cells[i].second;
        if (i) items.push_back(rectangle(colors::borderSubtle)->frameSize(borders::pixels, std::nullopt));
        items.push_back(cell);
    }
    return card(hstack(std::move(items), 0)->fixed(false, true)->fillWidth())->id("experience.statistics");
}

NodePtr ClientView::servers(const ld::ExperienceEntry& e, const ld::ExperienceDetails* details) {
    auto error = store->detailsError.find(e.id);
    if (error != store->detailsError.end())
        return emptyState("ld-icon-status-warning", error->second)->id("experience.servers.error");
    if (!details) return loading(tr("Cerco i server…"));
    if (details->servers.empty()) return emptyState("ld-icon-app-players", ld::localization::translate(ld::rules::serverList::emptyTitle))->id("experience.servers.empty");
    std::vector<NodePtr> rows;
    for (const auto& server : details->servers) {
        bool full = server.full();
        auto words = vstack({text(tr("Server {0}", server.sequence), type::label), text(tr("{0} su {1}", server.players, server.capacity), type::small, colors::textTertiary)}, spacing::s1, HAlign::leading);
        auto enter = button("experience.join." + std::to_string(server.sequence), full ? tr("Pieno") : tr("Entra"), [this, e, id = ld::ServerId(server.id)] { store->play(e, id); }, full ? ButtonRole::secondary : ButtonRole::success, ButtonSize::small)->disabled(full || store->join->busy());
        rows.push_back(hstack({icon("ld-icon-app-players", iconSize::large), words, spacer(spacing::s6), enter}, spacing::s6)->paddingHorizontal(spacing::s6)->paddingVertical(spacing::s3));
    }
    return group(std::move(rows))->id("experience.servers");
}

NodePtr ClientView::experience(const ld::ExperienceEntry& entry) {
    ld::ExperienceEntry e = entry;
    double width = surfaceWidth();
    if (requestedDetails != e.id) {
        requestedDetails = e.id;
        store->loadDetails(e.id).start();
    }
    auto found = store->details.find(e.id);
    const ld::ExperienceDetails* details = found == store->details.end() ? nullptr : &found->second;
    const ld::ExperienceFacts* known = details ? &details->experience : nullptr;
    double coverWidth = width >= breakpoints::wide ? metrics::coverWide : metrics::coverCompact;
    double coverHeight = std::round(coverWidth * 9 / 16);
    Image shot = thumbnail ? thumbnail(e.id) : Image{};
    Color tint = genreColor(e.genre);
    NodePtr mirror = shot.valid() ? reflected(shot, coverWidth, metrics::reflection) : gradient({tint.opacity(tokens::opacity::reflection), tint.opacity(0)}, metrics::frameRadius)->frameSize(coverWidth, metrics::reflection);
    auto picture = vstack({frameImage(cover(e, coverWidth, coverHeight))->id("experience.cover"), mirror->padding(Insets{borders::focus, 0, 0, 0})}, 0, HAlign::leading);
    std::string author = known ? known->author : e.author;
    std::string genreName = ld::genreTitle(known ? known->genre : e.genre);
    std::vector<NodePtr> byline;
    std::string owner = known && !known->owner.empty() ? known->owner : e.owner;
    if (!author.empty()) {
        auto by = text(tr("di {0}", author), type::strong, colors::value);
        byline.push_back(owner.empty() ? by : plainButton("experience.author", by, [this, owner, author] { openPlayer(owner, author); }, Cursor::pointer)->help(tr("Profilo di {0}", author)));
    }
    byline.push_back(text(author.empty() ? genreName : "· " + genreName, type::small, colors::textTertiary));
    auto heading = hstack({appIcon(badge(e), e.genre, metrics::detailIcon)->id("experience.icon"), vstack({embossed(lineLimited(text(known ? known->title : e.title, type::display), 2)->fixed(false, true), emboss::light), hstack(std::move(byline), spacing::s2)}, spacing::s2, HAlign::leading)}, spacing::s6);
    std::vector<NodePtr> column{heading};
    column.push_back(playButton("experience.play", tr("Gioca"), [this, e] { store->play(e); })->disabled(store->join->busy())->help(tr("Entra in un server con posto libero")));
    if (e.categories.count(ld::ExperienceScope::mine)) column.push_back(button("experience.studio", hstack({spacer(), text(tr("Apri in Studio")), spacer()}, 0), [this, e] { store->openStudio(e.id); })->help(tr("Modifica questa esperienza in Ludissey Studio")));
    auto side = vstack(std::move(column), spacing::s6, HAlign::leading)->fillWidth(Alignment::topLeading());
    bool info = store->experienceTab != "Server";
    std::vector<NodePtr> rows{backButton("experience.back", backTitle(), [this] {
                                  store->goBack();
                                  invalidate();
                              })->padding(Insets{0, 0, spacing::s8, 0}),
                              hstack({picture, side}, spacing::s12, VAlign::top)->padding(Insets{0, 0, spacing::s6, 0}), facts(e, details)->padding(Insets{0, 0, spacing::s8, 0}),
                              centered(segments("experience.tabs", {tr("Informazioni"), tr("Server")}, info ? 0 : 1, [this](int i) {
                                  store->experienceTab = i == 0 ? "Informazioni" : "Server";
                                  invalidate();
                              }))->padding(Insets{0, 0, spacing::s8, 0})};
    if (info) rows.push_back(card(text(e.description.empty() ? tr("Nessuna descrizione.") : e.description, type::body, e.description.empty() ? colors::textTertiary : colors::textPrimary)->fixed(false, true)->padding(spacing::s6)->fillWidth(Alignment::leading())));
    else {
        bool busy = store->detailsLoading.count(e.id) > 0;
        std::vector<NodePtr> header{groupTitle(tr("Server attivi"))->fixed(true, true), spacer(0.0)};
        if (busy) header.push_back(loading());
        header.push_back(button("experience.refreshServers", tr("Aggiorna"), [this, id = e.id] { store->loadDetails(id).start(); }, ButtonRole::secondary, ButtonSize::small)->disabled(busy));
        rows.push_back(hstack(std::move(header), spacing::s4)->padding(Insets{0, 0, spacing::s3, 0}));
        rows.push_back(servers(e, details));
    }
    return vstack(std::move(rows), 0, HAlign::leading);
}

void ClientView::openOwnProfile() {
    std::string name = store->profile ? store->profile->username : std::string();
    leave([this, name] { store->openProfile(store->playerId, name); });
}

void ClientView::openPlayer(const std::string& id, const std::string& username) {
    if (id == store->playerId) {
        openOwnProfile();
        return;
    }
    leave([this, id, username] { store->openProfile(id, username); });
}

NodePtr ClientView::profileActions(const ld::PlayerPage& page) {
    std::vector<NodePtr> actions;
    if (page.self) return hstack(std::move(actions), spacing::s4);
    if (page.isFriend)
        actions.push_back(button("profile.remove", tr("Rimuovi dagli amici"), [this, id = page.id, name = page.username] {
                              auto owner = store;
                              store->dialog = ld::DialogRequest{tr("Rimuovere {0}?", name), tr("Non sarete più amici. Puoi sempre richiederlo di nuovo."), {{tr("Annulla"), ld::DialogRole::secondary, [] {}}, {tr("Rimuovi"), ld::DialogRole::destructive, [owner, id] { owner->removeFriend(id); }}}};
                              invalidate();
                          }, ButtonRole::destructive)->disabled(store->busy));
    else if (page.requesting) {
        actions.push_back(button("profile.decline", tr("Rifiuta"), [this, id = page.id] { store->respond(id, false); })->disabled(store->busy));
        actions.push_back(button("profile.accept", tr("Accetta"), [this, id = page.id] { store->respond(id, true); }, ButtonRole::primary)->disabled(store->busy));
    } else if (page.requested)
        actions.push_back(button("profile.requested", tr("Richiesta inviata"), [] {})->disabled());
    else
        actions.push_back(button("profile.add", tr("Aggiungi agli amici"), [this, id = page.id] { store->requestFriendById(id); }, ButtonRole::primary)->disabled(store->busy));
    return hstack(std::move(actions), spacing::s4);
}

NodePtr ClientView::profilePage() {
    std::string id = store->profileId.value_or("");
    double width = surfaceWidth();
    std::vector<NodePtr> rows{backButton("profile.back", backTitle(), [this] {
                                  store->goBack();
                                  invalidate();
                              })->padding(Insets{0, 0, spacing::s8, 0})};
    auto found = store->profiles.find(id);
    if (found == store->profiles.end()) {
        auto failed = store->profilesError.find(id);
        if (failed != store->profilesError.end()) rows.push_back(emptyState("ld-icon-status-warning", failed->second)->id("profile.error"));
        else rows.push_back(centered(loading())->padding(spacing::s12)->id("profile.loading"));
        return vstack(std::move(rows), 0, HAlign::leading);
    }
    const ld::PlayerPage& page = found->second;
    if (page.self) {
        std::vector<NodePtr> strip{hstack({text(tr("Così vedono il tuo profilo gli altri giocatori."), type::smallStrong, colors::value)->fixed(false, true), spacer(0.0),
                                           button("profile.editProfile", tr("Modifica profilo"), [this] {
                                               settingsTab = 1;
                                               store->navigate(ld::Route::settings);
                                           }, ButtonRole::secondary, ButtonSize::small),
                                           button("profile.editAvatar", tr("Modifica avatar"), [this] { store->navigate(ld::Route::avatar); }, ButtonRole::secondary, ButtonSize::small)},
                                          spacing::s4)};
        if (!page.publicProfile) strip.push_back(text(tr("Il tuo profilo è privato: chi non è tuo amico non vede la descrizione e gli amici."), type::small, colors::textSecondary)->fixed(false, true));
        rows.push_back(vstack(std::move(strip), spacing::s3, HAlign::leading)->padding(spacing::s6)->fillWidth(Alignment::leading())->background(roundedRectangle(metrics::groupRadius, colors::accentSurface))->id("profile.own")->padding(Insets{0, 0, spacing::s8, 0}));
    }
    std::string since = page.createdAt ? tr("Su Ludissey dal {0}", ld::localization::date(*page.createdAt)) : std::string();
    std::vector<NodePtr> words{embossed(lineLimited(text(page.username, type::pageTitle), 1), emboss::light)->id("profile.name"),
                               hstack({presenceDot(page.online), text(page.online ? tr("Online") : tr("Offline"), type::small, colors::textTertiary)}, spacing::s2)};
    if (!since.empty()) words.push_back(text(since, type::small, colors::textTertiary));
    double bustSide = tokens::size::profileBust;
    auto portrait = zstack({frameImage(bust(page.username, bustSide)), presence(page.online, tokens::size::profilePresence)->offset(spacing::s1, spacing::s1)}, Alignment::bottomTrailing());
    rows.push_back(group({hstack({portrait, vstack(std::move(words), spacing::s2, HAlign::leading), spacer(0.0), profileActions(page)}, spacing::s8)->padding(spacing::s8)->fillWidth(Alignment::leading())})->id("profile.header")->padding(Insets{0, 0, spacing::s8, 0}));
    rows.push_back(centered(segments("profile.tabs", {tr("Informazioni"), tr("Creazioni")}, profileTab, [this](int i) {
                       profileTab = i;
                       invalidate();
                   }))->padding(Insets{0, 0, spacing::s8, 0}));
    if (profileTab == 1) {
        std::vector<NodePtr> tiles;
        for (const auto& world : page.experiences) {
            const auto& catalog = store->experienceCatalog();
            auto listed = std::find_if(catalog.begin(), catalog.end(), [&](const ld::ExperienceEntry& e) { return e.id == world.id; });
            ld::ExperienceEntry entry = listed != catalog.end() ? *listed : ld::ExperienceEntry{world.id, world.title, "", world.author, world.genre.value_or(std::string(ld::rules::defaultGenre)), world.serverSize.value_or(ld::rules::serverSize::standard), world.createdAt, world.updatedAt, {ld::ExperienceScope::all}, true, std::nullopt, world.icon, world.thumbnail, world.owner};
            tiles.push_back(experienceCard(entry, metrics::tileStandard));
        }
        if (tiles.empty()) rows.push_back(page.self ? emptyState("ld-icon-app-experiences", tr("Non hai ancora pubblicato esperienze."), button("profile.studio", tr("Apri Ludissey Studio"), [this] { store->openStudio(); }, ButtonRole::primary)) : emptyState("ld-icon-app-experiences", tr("Non ha ancora pubblicato esperienze.")));
        else rows.push_back(grid(std::move(tiles), metrics::tileStandard, metrics::tileStandard, spacing::s8, spacing::s8)->id("profile.creations"));
        return vstack(std::move(rows), 0, HAlign::leading);
    }
    double figureWidth = tokens::size::profileFigureWidth, figureHeight = tokens::size::profileFigureHeight;
    Image shot = figure ? figure(page.id) : Image{};
    NodePtr body = shot.valid() ? ui::picture(shot, true) : rectangle(colors::surfaceAlt);
    NodePtr mirror = shot.valid() ? reflected(shot, figureWidth, metrics::reflection) : gradient({colors::surfaceAlt.opacity(tokens::opacity::reflection), colors::surfaceAlt.opacity(0)}, metrics::frameRadius)->frameSize(figureWidth, metrics::reflection);
    auto avatarColumn = vstack({groupTitle(tr("Avatar")), frameImage(body->frameSize(figureWidth, figureHeight)->clipped())->id("profile.figure"), mirror->padding(Insets{borders::focus, 0, 0, 0})}, spacing::s3, HAlign::leading);
    std::vector<NodePtr> details;
    auto privateBox = [] { return group({hstack({icon("ld-icon-status-warning", iconSize::compact), text(tr("Profilo privato"), type::body, colors::textTertiary)}, spacing::s3)->padding(spacing::s6)->fillWidth(Alignment::leading())})->id("profile.private"); };
    details.push_back(groupTitle(tr("Descrizione")));
    if (page.hidden()) details.push_back(privateBox());
    else details.push_back(group({text(page.bio->empty() ? tr("Nessuna descrizione.") : *page.bio, type::body, page.bio->empty() ? colors::textTertiary : colors::textPrimary)->fixed(false, true)->padding(spacing::s6)->fillWidth(Alignment::leading())})->id("profile.bio"));
    std::vector<NodePtr> friendsHeader{groupTitle(tr("Amici ({0})", static_cast<long long>(page.friends))), spacer(0.0)};
    if (page.self) friendsHeader.push_back(button("profile.friends.all", tr("Vedi tutti"), [this] {
                                                friendsTab = 0;
                                                store->navigate(ld::Route::friends);
                                            }, ButtonRole::secondary, ButtonSize::small));
    details.push_back(hstack(std::move(friendsHeader), spacing::s4)->padding(Insets{spacing::s6, 0, 0, 0}));
    if (!page.friendList) details.push_back(privateBox());
    else if (page.friendList->empty()) details.push_back(group({text(tr("Nessun amico, per ora."), type::body, colors::textTertiary)->padding(spacing::s6)->fillWidth(Alignment::leading())}));
    else {
        std::vector<NodePtr> people;
        double side = tokens::size::profileFriend;
        for (const auto& f : *page.friendList)
            people.push_back(plainButton("profile.friend." + f.id, vstack({bust(f.username, side), lineLimited(text(f.username, type::smallStrong), 1)->frameSize(side + spacing::s8, std::nullopt)}, spacing::s2), [this, id = f.id, name = f.username] { openPlayer(id, name); }, Cursor::pointer)->help(f.username));
        people.push_back(spacer(0.0));
        details.push_back(group({hstack(std::move(people), spacing::s6, VAlign::top)->padding(spacing::s6)->fillWidth(Alignment::leading())})->id("profile.friends"));
    }
    details.push_back(groupTitle(tr("Statistiche"))->padding(Insets{spacing::s6, 0, 0, 0}));
    std::vector<NodePtr> stats;
    if (page.createdAt) stats.push_back(groupRow(text(tr("Su Ludissey dal")), text(ld::localization::date(*page.createdAt), type::body, colors::value)));
    stats.push_back(groupRow(text(tr("Esperienze pubblicate")), text(ld::localization::number(static_cast<int>(page.experiences.size())), type::body, colors::value)));
    stats.push_back(groupRow(text(tr("Amici")), text(ld::localization::number(page.friends), type::body, colors::value)));
    details.push_back(group(std::move(stats))->id("profile.stats"));
    auto detailColumn = vstack(std::move(details), spacing::s3, HAlign::leading)->fillWidth(Alignment::topLeading());
    if (width >= breakpoints::columns) rows.push_back(hstack({avatarColumn->frameSize(tokens::size::profileColumn, std::nullopt, Alignment::topLeading()), detailColumn}, spacing::s12, VAlign::top));
    else rows.push_back(vstack({avatarColumn, detailColumn}, spacing::s8, HAlign::leading));
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr ClientView::friends() {
    std::vector<ld::Friend> shown;
    for (const auto& f : store->friends)
        if (store->matchesSearch(f.username)) shown.push_back(f);
    std::stable_sort(shown.begin(), shown.end(), [](const auto& a, const auto& b) { return a.online && !b.online; });
    std::vector<ld::FriendRequest> pending;
    for (const auto& r : store->requests)
        if (store->matchesSearch(r.username)) pending.push_back(r);
    bool searching = !store->searchText().empty();
    auto send = [this] {
        if (store->busy) return;
        std::size_t length = ld::utf8Length(ld::trim(friendName));
        if (length < 3 || length > 20) {
            friendError = tr("Il nome utente deve avere da 3 a 20 caratteri.");
            if (focusField) focusField("friends.username");
            invalidate();
            return;
        }
        friendError.clear();
        store->requestFriend(friendName);
    };
    std::vector<NodePtr> rows{pageTitle(tr("Amici")),
                              hstack({field("friends.username", tr("Nome utente"), friendName, false, send)->frameSize(metrics::field, std::nullopt), button("friends.request", tr("Invia richiesta"), send, ButtonRole::primary), spacer(0.0)}, spacing::s4)->padding(Insets{0, 0, friendError.empty() ? spacing::s8 : spacing::s2, 0}),
                              segments("friends.tabs", {tr("Amici ({0})", static_cast<long long>(store->friends.size())), tr("Richieste ({0})", static_cast<long long>(store->requests.size()))}, friendsTab, [this](int i) {
                                  friendsTab = i;
                                  invalidate();
                              })->padding(Insets{0, 0, spacing::s8, 0})};
    if (!friendError.empty()) rows.insert(rows.begin() + 2, hstack({icon("ld-icon-status-warning", iconSize::compact), text(friendError, type::small, colors::danger)->fixed(false, true)}, spacing::s2)->padding(Insets{0, 0, spacing::s6, 0})->id("friends.error"));
    double side = metrics::friendBust;
    double cell = friendsTab == 0 ? metrics::friendCard + spacing::s12 : std::max(metrics::friendCard + spacing::s12, metrics::tileStandard);
    std::vector<NodePtr> cards;
    if (friendsTab == 0) {
        for (const auto& f : shown) {
            auto content = vstack({bust(f.username, side), lineLimited(text(f.username, type::strong), 1), hstack({presenceDot(f.online), text(f.online ? tr("Online") : tr("Offline"), type::caption, colors::textTertiary)}, spacing::s2)}, spacing::s3)
                               ->padding(spacing::s6)
                               ->frameSize(cell, std::nullopt);
            std::string cardId = "friends.card." + f.id, removeId = "friends.remove." + f.id;
            Surface* surface = Surface::active();
            bool visible = surface && (surface->hovered(cardId) || surface->hovered(removeId) || surface->focused(removeId));
            auto remove = button(removeId, tr("Rimuovi"), [this, id = f.id, name = f.username] {
                              auto owner = store;
                              store->dialog = ld::DialogRequest{tr("Rimuovere {0}?", name), tr("Non sarete più amici. Puoi sempre richiederlo di nuovo."), {{tr("Annulla"), ld::DialogRole::secondary, [] {}}, {tr("Rimuovi"), ld::DialogRole::destructive, [owner, id] { owner->removeFriend(id); }}}};
                              invalidate();
                          }, ButtonRole::destructive, ButtonSize::small)
                              ->help(tr("Rimuovi dagli amici"))
                              ->disabled(store->busy)
                              ->opacity(visible ? 1 : 0)
                              ->padding(spacing::s3);
            auto open = plainButton("friends.open." + f.id, card(content), [this, id = f.id, name = f.username] { openPlayer(id, name); }, Cursor::pointer)->help(tr("Profilo di {0}", f.username));
            cards.push_back(zstack({open, remove}, Alignment::topTrailing())->onHover(cardId, [this](bool) { invalidate(); })->id(cardId));
        }
        if (cards.empty()) rows.push_back(emptyState(searching ? "ld-icon-app-search" : "ld-icon-app-friends", searching ? tr("Nessun risultato per «{0}».", store->searchQuery) : tr("Non hai ancora amici. Cercali per nome utente.")));
    } else {
        for (const auto& r : pending) {
            auto who = plainButton("friends.requestOpen." + r.id, vstack({bust(r.username, side), lineLimited(text(r.username, type::strong), 1)}, spacing::s3), [this, id = r.id, name = r.username] { openPlayer(id, name); }, Cursor::pointer)->help(tr("Profilo di {0}", r.username));
            auto content = vstack({who,
                                   hstack({button("friends.accept." + r.id, tr("Accetta"), [this, id = r.id] { store->respond(id, true); }, ButtonRole::success, ButtonSize::small), button("friends.reject." + r.id, tr("Rifiuta"), [this, id = r.id] { store->respond(id, false); }, ButtonRole::secondary, ButtonSize::small)}, spacing::s2)},
                                  spacing::s3)
                               ->padding(spacing::s6)
                               ->frameSize(cell, std::nullopt)
                               ->disabled(store->busy);
            cards.push_back(card(content)->id("friends.request." + r.id));
        }
        if (cards.empty()) rows.push_back(emptyState(searching ? "ld-icon-app-search" : "ld-icon-app-friends", searching ? tr("Nessun risultato per «{0}».", store->searchQuery) : tr("Nessuna richiesta di amicizia.")));
    }
    if (!cards.empty()) rows.push_back(grid(std::move(cards), cell, cell, spacing::s8, spacing::s8)->id("friends.grid"));
    return vstack(std::move(rows), 0, HAlign::leading);
}

NodePtr ClientView::settings() {
    if (!bioLoaded) {
        bio = store->preferences.bio;
        bioLoaded = true;
    }
    auto label = [](const std::string& title) { return text(title); };
    auto switchRow = [this, label](const std::string& id, const std::string& title, const std::string& detail, bool value, std::function<void(bool)> change) {
        return groupRow(label(title), toggle(id, value, std::move(change))->disabled(store->busy), detail);
    };
    auto preference = [this, switchRow](const std::string& id, const std::string& title, const std::string& detail, bool ld::PlayerPreferences::*member) {
        return switchRow(id, title, detail, store->preferences.*member, [this, member](bool value) {
            store->setPreference(member, value);
            invalidate();
        });
    };
    std::vector<NodePtr> rows;
    auto gap = [] { return spacer(spacing::s8)->frameSize(std::nullopt, spacing::s8); };
    if (settingsTab == 0) {
        auto passwordRow = vstack({groupRow(label(tr("Password")), nullptr, tr("Ti mandiamo un link all’email del tuo account.")),
                                   hstack({field("settings.email", tr("Email"), email)->fillWidth(), button("settings.password", tr("Invia il link"), [this] { store->passwordEmail(email); })->disabled(store->busy)}, spacing::s4)->padding(Insets{0, spacing::s6, spacing::s6, spacing::s6})},
                                  0, HAlign::leading);
        rows.push_back(group({passwordRow}));
        rows.push_back(gap());
        rows.push_back(group({plainButton("settings.website", groupRow(label(tr("Ludissey sul web")), disclosure(std::string(ld::rules::site::host)), tr("Accedi con lo stesso account sul sito.")), [this] { store->openWebsite(); }, Cursor::pointer)}));
        rows.push_back(gap());
        rows.push_back(button("settings.logout", hstack({spacer(), text(tr("Esci dall’account")), spacer()}, 0), [this] {
                           store->confirmLogout();
                           invalidate();
                       }, ButtonRole::destructive, ButtonSize::large)->disabled(store->busy));
    } else if (settingsTab == 1) {
        std::size_t count = ld::utf8Length(bio);
        rows.push_back(group({preference("settings.publicProfile", tr("Profilo pubblico"), tr("Tutti possono vedere nome, avatar e descrizione."), &ld::PlayerPreferences::publicProfile)}));
        rows.push_back(gap());
        rows.push_back(groupTitle(tr("Descrizione")));
        rows.push_back(group({vstack({field("settings.bio", tr("Scrivi qualcosa su di te"), bio, false, [this] { store->setBio(bio); }, false, true)->frameSize(std::nullopt, metrics::multilineField)->fillWidth(),
                                      hstack({text(tr("{0} di {1} caratteri", static_cast<long long>(count), ld::bioCharacters), type::small, colors::textTertiary), spacer(0.0), button("settings.bio.save", tr("Salva"), [this] { store->setBio(bio); }, ButtonRole::primary, ButtonSize::small)->disabled(store->busy || count > static_cast<std::size_t>(ld::bioCharacters) || bio == store->preferences.bio)}, spacing::s4)},
                                     spacing::s4, HAlign::leading)
                                  ->padding(spacing::s6)}));
    } else if (settingsTab == 2) {
        rows.push_back(group({preference("settings.friendNotifications", tr("Richieste di amicizia"), tr("Avvisami quando qualcuno vuole aggiungermi agli amici."), &ld::PlayerPreferences::friendNotifications)}));
    } else {
        double volume = ld::audio::masterVolume();
        auto volumeControl = hstack({slider("settings.volume", volume, 0, 1, 0.05, colors::accent, [this](double chosen) {
                                         ld::audio::setMasterVolume(chosen);
                                         ld::local_preferences::setNumber(ld::local_preferences::keys::masterVolume, chosen);
                                         invalidate();
                                     })->frameSize(metrics::field, std::nullopt)->id("settings.volume"),
                                     text(volume <= 0 ? tr("Muto") : tr("{0}%", static_cast<long long>(std::lround(volume * 100))), type::body, colors::value)->frameSize(spacing::s16 + spacing::s8, std::nullopt, Alignment::trailing())},
                                    spacing::s4);
        bool chatVisible = ld::local_preferences::number(ld::local_preferences::keys::chatVisible).value_or(1) != 0;
        NativeField graphics;
        graphics.id = "settings.graphics";
        graphics.options.push_back(tr("Automatica"));
        for (const auto& level : ld::render::GraphicsLevel::ladder()) graphics.options.push_back(ld::localization::translate(level.title));
        double stored = ld::local_preferences::number(ld::local_preferences::keys::graphicsChoice).value_or(-1);
        graphics.selectedOption = stored < 0 ? 0 : static_cast<int>(stored) + 1;
        graphics.choose = [this](int index) {
            std::optional<int> level = index <= 0 ? std::nullopt : std::optional<int>(index - 1);
            ld::local_preferences::setNumber(ld::local_preferences::keys::graphicsChoice, level ? *level : -1);
            if (graphicsChanged) graphicsChanged(level);
            invalidate();
        };
        NativeField language;
        language.id = "settings.language";
        language.options = {tr("Come il Mac"), "Italiano", "English"};
        double chosen = ld::local_preferences::number(ld::local_preferences::keys::language).value_or(-1);
        language.selectedOption = chosen < 0 ? 0 : static_cast<int>(chosen) + 1;
        language.choose = [this](int index) {
            ld::local_preferences::setNumber(ld::local_preferences::keys::language, index - 1);
            ld::localization::setLanguage(index <= 0 ? std::nullopt : std::optional<ld::Language>(index == 1 ? ld::Language::italian : ld::Language::english));
            invalidate();
        };
        rows.push_back(groupTitle(tr("Audio")));
        rows.push_back(group({groupRow(label(tr("Volume")), volumeControl), preference("settings.sounds", tr("Suoni dell’interfaccia"), "", &ld::PlayerPreferences::sounds)}));
        rows.push_back(gap());
        rows.push_back(groupTitle(tr("Gioco")));
        rows.push_back(group({switchRow("settings.chat", tr("Mostra la chat"), tr("Mostra la chat mentre giochi."), chatVisible, [this](bool value) {
                                  ld::local_preferences::setNumber(ld::local_preferences::keys::chatVisible, value ? 1 : 0);
                                  invalidate();
                              }),
                              groupRow(label(tr("Qualità grafica")), select(graphics)->frameSize(metrics::menu, std::nullopt), tr("Automatica: Ludissey sceglie la qualità giusta per il tuo Mac.")), groupRow(label(tr("Lingua")), select(language)->frameSize(metrics::menu, std::nullopt))}));
    }
    std::vector<NodePtr> column{pageTitle(tr("Impostazioni")), segments("settings.tabs", {tr("Account"), tr("Privacy"), tr("Notifiche"), tr("Audio e grafica")}, settingsTab, [this](int i) {
                                    settingsTab = i;
                                    invalidate();
                                })->padding(Insets{0, 0, spacing::s8, 0})};
    for (auto& row : rows) column.push_back(row);
    auto content = vstack(std::move(column), 0, HAlign::leading)->flexible(std::nullopt, metrics::settings, std::nullopt, std::nullopt, Alignment::topLeading());
    return content->fillWidth(Alignment::top())->id("settings.page");
}

ld::AvatarConfiguration ClientView::shownAvatar() const {
    if (store->avatarDraft) return *store->avatarDraft;
    if (auto current = store->currentAvatar()) return *current;
    return ld::AvatarConfiguration::standard("", std::string(ld::rules::avatar::standard::bodyPreset));
}

NodePtr ClientView::avatarTile(const ld::AvatarItem& item, bool worn, bool offered) {
    Image picture = itemImage ? itemImage(item) : Image{};
    bool face = item.category.name == "face";
    Color skin = Color::hex(std::string(ld::rules::avatar::standard::bodyColors::head));
    NodePtr preview = picture.valid() ? ui::picture(picture, false)->padding(spacing::s3) : icon("ld-icon-app-catalog", iconSize::empty);
    std::string id = "avatar.item." + item.id;
    bool free = item.free.value_or(false);
    std::function<void()> action = offered ? (free ? std::function<void()>([this, item] { store->acquire(item); }) : std::function<void()>()) : std::function<void()>([this, item] { store->toggle(item); });
    auto perform = [this, action] {
        if (action) action();
        invalidate();
    };
    NodePtr state = offered ? (free ? button(id + ".action", tr("Ottieni"), perform, ButtonRole::success, ButtonSize::small) : text(tr("Non disponibile"), type::caption, colors::textTertiary)) : button(id + ".action", worn ? tr("Togli") : tr("Indossa"), perform, worn ? ButtonRole::secondary : ButtonRole::primary, ButtonSize::small);
    double shot = metrics::itemHeight - lineHeight(type::smallStrong) - metrics::buttonSmall - 4 * spacing::s3;
    auto thumb = zstack({rectangle(face && picture.valid() ? skin : colors::surfaceAlt), preview})->frameSize(metrics::itemWidth - 2 * spacing::s3, shot)->clipped(metrics::bustRadius);
    auto content = vstack({thumb, lineLimited(text(item.name, type::smallStrong), 1), state}, spacing::s3)->padding(spacing::s3)->frameSize(metrics::itemWidth, metrics::itemHeight, Alignment::top());
    NodePtr tile = content->background(rectangle(colors::surface))->clipped(metrics::groupRadius)->overlay(strokeRectangle(worn ? colors::accent : colors::border, worn ? borders::focus : borders::pixels, metrics::groupRadius));
    if (!action) return tile;
    auto result = plainButton(id, tile, perform, Cursor::pointer);
    result->selected = worn;
    result->label = item.name;
    return result->help(item.name);
}

NodePtr ClientView::avatarEditor(const ld::AvatarConfiguration& shown) {
    auto update = [this](std::function<void(ld::AvatarConfiguration&)> change) {
        auto next = shownAvatar();
        change(next);
        store->avatarDraft = next;
        invalidate();
    };
    auto labelled = [](const std::string& title, NodePtr content) { return vstack({embossed(text(title, type::label, colors::textSecondary), emboss::light), content}, spacing::s3, HAlign::leading); };
    std::vector<NodePtr> items;
    if (avatarTab == 2) {
        items.push_back(button("avatar.colors.reset", tr("Colori predefiniti"), [update] { update([](auto& next) { next.bodyColors = ld::AvatarConfiguration::standard("", next.bodyPreset).bodyColors; }); }, ButtonRole::secondary, ButtonSize::small));
        for (auto part : ld::allBodyParts()) {
            std::vector<NodePtr> swatches;
            for (auto color : ld::rules::avatar::palette) {
                std::string hex(color);
                bool chosen = shown.color(part) == hex;
                double size = metrics::swatch + spacing::s3;
                Color tint = Color::hex(hex);
                auto swatch = canvas([tint, chosen](Canvas& c, const Frame& f) {
                                  c.fill(f, tint, metrics::bustRadius);
                                  c.gloss(f, metrics::bustRadius, tokens::opacity::iconTop, tokens::opacity::iconBottom);
                                  c.stroke(f, colors::bustEdge, borders::pixels, metrics::bustRadius);
                                  if (chosen) c.stroke(f.inset(-borders::focus, -borders::focus), colors::accent, borders::focus, metrics::bustRadius + borders::focus);
                              })->frameSize(size, size);
                auto entry = plainButton("avatar.color." + std::string(ld::bodyPartName(part)) + "." + hex, swatch, [update, part, hex] { update([part, hex](auto& next) { next.bodyColors[std::string(ld::bodyPartName(part))] = hex; }); })->help("#" + hex);
                entry->label = ld::bodyPartTitle(part) + " #" + hex + (chosen ? ", " + tr("selezionato") : "");
                swatches.push_back(entry);
            }
            items.push_back(labelled(ld::bodyPartTitle(part), grid(std::move(swatches), metrics::swatch + spacing::s3, metrics::swatch + spacing::s3, spacing::s2, spacing::s2))->id("avatar.color." + std::string(ld::bodyPartName(part))));
        }
        return vstack(std::move(items), spacing::s6, HAlign::leading);
    }
    auto group = avatarTab == 0 ? ld::AvatarGroup::clothing : avatarTab == 1 ? ld::AvatarGroup::body : ld::AvatarGroup::animations;
    if (group == ld::AvatarGroup::body) {
        NativeField name;
        name.id = "avatar.name";
        name.text = shown.displayName;
        name.prompt = tr("Nome visualizzato");
        name.changed = [update](const std::string& value) { update([value](auto& next) { next.displayName = value; }); };
        std::vector<std::string> presets;
        int selected = 0;
        for (std::size_t i = 0; i < ld::rules::avatar::bodyPresets.size(); ++i) {
            presets.push_back(ld::localization::translate(ld::rules::avatar::bodyPresetTitles[i]));
            if (shown.bodyPreset == ld::rules::avatar::bodyPresets[i]) selected = static_cast<int>(i);
        }
        items.push_back(hstack({labelled(tr("Nome visualizzato"), styledField(name)->frameSize(metrics::field, std::nullopt)), labelled(tr("Corporatura"), segments("avatar.body", presets, selected, [update](int i) { update([i](auto& next) { next.bodyPreset = std::string(ld::rules::avatar::bodyPresets[static_cast<std::size_t>(i)]); }); }))}, spacing::s12, VAlign::top));
    }
    auto categories = ld::categoriesIn(group);
    auto found = std::find(categories.begin(), categories.end(), store->avatarCategory);
    int index = found == categories.end() ? 0 : static_cast<int>(found - categories.begin());
    auto category = categories[static_cast<std::size_t>(index)];
    if (categories.size() > 1) {
        NativeField picker;
        picker.id = "avatar.category";
        for (const auto& c : categories) picker.options.push_back(c.title());
        picker.selectedOption = index;
        picker.choose = [this, categories](int i) {
            store->avatarCategory = categories[static_cast<std::size_t>(i)];
            invalidate();
        };
        items.push_back(select(picker)->frameSize(metrics::field, std::nullopt)->id("avatar.categories"));
    }
    if (store->avatarRefusal) items.push_back(text(*store->avatarRefusal, type::small, colors::danger));
    auto owned = store->items(category);
    if (owned.empty()) {
        bool searching = !store->searchText().empty();
        items.push_back(emptyState("ld-icon-app-catalog", searching ? tr("Nessun risultato per «{0}».", store->searchQuery) : tr("Non hai ancora niente in {0}.", category.title()))->id("avatar.empty." + category.name));
    } else {
        std::vector<NodePtr> tiles;
        for (const auto& item : owned) tiles.push_back(avatarTile(item, shown.wearing(item.id), false));
        items.push_back(grid(std::move(tiles), metrics::itemWidth, metrics::itemWidth, spacing::s6, spacing::s6));
    }
    return vstack(std::move(items), spacing::s6, HAlign::leading);
}

NodePtr ClientView::avatar() {
    double width = surfaceWidth();
    auto shown = shownAvatar();
    bool changed = store->avatarDraft != store->avatar;
    bool stacked = width < breakpoints::small;
    double previewWidth = width < breakpoints::narrow ? metrics::avatarPreviewNarrow : metrics::avatarPreviewWidth;
    auto preview = frameImage(zstack({rectangle(colors::surfaceAlt), loading(), viewport("avatar.preview")})->frameSize(previewWidth, metrics::avatarPreviewHeight))->id("avatar.preview.frame");
    std::vector<NodePtr> actions{button("avatar.save", tr("Salva"), [this] { store->saveAvatar(); }, ButtonRole::primary)->disabled(store->busy || !changed), button("avatar.revert", tr("Annulla"), [this] {
                                                                                                                                                  store->revertAvatar();
                                                                                                                                                  invalidate();
                                                                                                                                              })->disabled(store->busy || !changed)};
    if (changed) actions.push_back(text(tr("Modifiche non salvate"), type::small, colors::textTertiary));
    std::vector<NodePtr> offers;
    auto owned = store->avatarCollection.owned();
    for (const auto& item : store->avatarCollection.catalog)
        if (!owned.count(item.id) && store->matchesSearch(item.name)) offers.push_back(avatarTile(item, false, true));
    NodePtr catalog = offers.empty() ? emptyState("ld-icon-app-catalog", store->avatarCollection.catalog.empty() ? tr("Il catalogo è vuoto, per ora.") : tr("Hai già tutto il catalogo!"))->id("avatar.catalog.empty") : grid(std::move(offers), metrics::itemWidth, metrics::itemWidth, spacing::s6, spacing::s6);
    auto editor = vstack({segments("avatar.tabs", {tr("Abbigliamento"), tr("Corpo"), tr("Colori"), tr("Animazioni")}, avatarTab, [this](int i) {
                              avatarTab = i;
                              invalidate();
                          }),
                          avatarEditor(shown), hstack(std::move(actions), spacing::s4)->padding(Insets{spacing::s2, 0, 0, 0}), vstack({sectionTitle(tr("Catalogo")), catalog}, 0, HAlign::leading)},
                         spacing::s8, HAlign::leading)
                      ->fillWidth(Alignment::topLeading());
    NodePtr workshop = stacked ? vstack({preview, editor}, spacing::s8, HAlign::leading) : hstack({preview, editor}, spacing::s12, VAlign::top);
    return vstack({pageTitle(tr("Avatar")), workshop}, 0, HAlign::leading);
}

NodePtr ClientView::joinOverlay() {
    auto join = store->join;
    auto failure = join->failure();
    std::string title = failure ? ld::failureTitle(*failure) : join->experience() ? join->experience()->title : std::string("Ludissey");
    std::string message = failure ? ld::failureMessage(*failure) : ld::phaseMessage(join->phase());
    NodePtr art;
    if (!failure && join->experience()) art = appIcon(badge(*join->experience()), join->experience()->genre, metrics::joinIcon);
    std::vector<DialogButton> actions{{failure ? tr("Chiudi") : tr("Annulla"), ButtonRole::secondary, [join] { join->cancel(); }, "join.cancel"}};
    auto next = failure ? ld::failureAction(*failure) : ld::FailureAction::none;
    if (next == ld::FailureAction::play && join->experience())
        actions.push_back({tr("Gioca"), ButtonRole::primary, [this, join, entry = *join->experience()] {
                               join->cancel();
                               store->play(entry);
                           },
                           "join.play"});
    else if (next == ld::FailureAction::retry) actions.push_back({tr("Riprova"), ButtonRole::primary, [join] { join->retry(); }, "join.retry"});
    return dialog(title, message, std::move(actions), failure.has_value(), art)->id("join.overlay");
}

}
