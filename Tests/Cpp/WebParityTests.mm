#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include "Golden.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/ClientView.hpp"
#include "Interface/Components.hpp"
#include "Interface/Surface.hpp"
#include "Pixels.hpp"
#include "Sample.hpp"
#include "ServerHarness.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <chrono>

@interface LDParityLoad : NSObject <WKNavigationDelegate>
@property(nonatomic) BOOL finished;
@end

@implementation LDParityLoad
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
    self.finished = YES;
}
@end

namespace {

bool pump(double seconds, const std::function<bool()>& done) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        ld::fallbackQueue().pump(0.002);
    }
    return true;
}

std::string evaluate(WKWebView* web, const std::string& source) {
    auto value = std::make_shared<std::string>();
    auto finished = std::make_shared<bool>(false);
    [web evaluateJavaScript:[NSString stringWithUTF8String:source.c_str()]
          completionHandler:^(id result, NSError* error) {
              if (error) {
                  NSString* message = error.userInfo[@"WKJavaScriptExceptionMessage"] ?: error.localizedDescription;
                  *value = std::string("errore: ") + message.UTF8String;
              } else *value = result ? [[NSString stringWithFormat:@"%@", result] UTF8String] : "";
              *finished = true;
          }];
    pump(20, [finished] { return *finished; });
    return *value;
}

pixels::Pixels snapshot(WKWebView* web, int width, int height) {
    auto out = std::make_shared<pixels::Pixels>();
    auto finished = std::make_shared<bool>(false);
    [web takeSnapshotWithConfiguration:nil
                     completionHandler:^(NSImage* image, NSError*) {
                         if (image) {
                             CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
                             pixels::Pixels made{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 0)};
                             CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                             CGContextRef context = CGBitmapContextCreate(made.rgba.data(), static_cast<size_t>(width), static_cast<size_t>(height), 8, static_cast<size_t>(width * 4), space, static_cast<uint32_t>(kCGImageAlphaPremultipliedLast));
                             CGContextDrawImage(context, CGRectMake(0, 0, width, height), cg);
                             CGContextRelease(context);
                             CGColorSpaceRelease(space);
                             *out = std::move(made);
                         }
                         *finished = true;
                     }];
    pump(15, [finished] { return *finished; });
    return *out;
}

double similarity(const pixels::Pixels& a, const pixels::Pixels& b, int top, int bottom) {
    if (a.width != b.width || a.height != b.height || a.width == 0) return 0;
    auto light = [](const pixels::Pixels& p, int x, int y) {
        const std::uint8_t* c = &p.rgba[static_cast<std::size_t>((y * p.width + x) * 4)];
        return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2];
    };
    const int window = 8;
    const double c1 = 6.5025, c2 = 58.5225;
    double total = 0;
    int count = 0;
    for (int y = top; y + window <= bottom; y += window)
        for (int x = 0; x + window <= a.width; x += window) {
            double ma = 0, mb = 0;
            for (int j = 0; j < window; ++j)
                for (int i = 0; i < window; ++i) {
                    ma += light(a, x + i, y + j);
                    mb += light(b, x + i, y + j);
                }
            ma /= window * window;
            mb /= window * window;
            double va = 0, vb = 0, cov = 0;
            for (int j = 0; j < window; ++j)
                for (int i = 0; i < window; ++i) {
                    double da = light(a, x + i, y + j) - ma, db = light(b, x + i, y + j) - mb;
                    va += da * da;
                    vb += db * db;
                    cov += da * db;
                }
            va /= window * window - 1;
            vb /= window * window - 1;
            cov /= window * window - 1;
            total += ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            ++count;
        }
    return count ? total / count : 0;
}

std::filesystem::path stageSite(const std::string& label) {
    auto site = std::filesystem::temp_directory_path() / ("Ludissey " + label + " " + ld::makeUuid());
    std::filesystem::copy(golden::root() / "Web", site, std::filesystem::copy_options::recursive);
    std::filesystem::copy(golden::root() / "Resources/Backgrounds", site / "backgrounds", std::filesystem::copy_options::recursive);
    std::filesystem::copy(golden::root() / "Resources/Faces", site / "faces", std::filesystem::copy_options::recursive);
    std::filesystem::copy(golden::root() / "Resources/Portraits", site / "portraits", std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(site / "icons");
    for (const auto& [name, file] : ui::icons::semanticFiles()) {
        if (name.rfind("ld-icon-app-", 0) != 0 && name.rfind("ld-icon-status-", 0) != 0) continue;
        for (int side : {16, 20, 22, 24, 32, 34, 40, 44, 48, 64, 68}) {
            ui::Image small = ui::icons::sized(name, side);
            if (!small.valid()) continue;
            auto pictured = pixels::draw(static_cast<CGImageRef>(small.handle.get()), side, side, 0);
            pixels::save(pictured, site / "icons" / (std::filesystem::path(file).stem().string() + "-" + std::to_string(side) + ".png"));
        }
    }
    return site;
}

std::string backendShim() {
    std::string shim = "var TestBackend=(function(){var handlers={};var currentPlayerId=null;";
    for (const char* name : {"CloudScript/Rules.js", "CloudScript/Avatar.js", "CloudScript/Ludissey.js", "Tests/Emulator/PlayFabEmulator.js", "Tests/Emulator/PlayFabWeb.js"}) shim += golden::readText(golden::root() / name);
    return shim + "return {emulator:emulator,accounts:accounts,invoke:webInvoke,enroll:webEnroll,baseplate:baseplate};})();";
}

const char* const seed = R"JS(window.ludisseyDone=false;window.ludisseyError='';(async()=>{const B=TestBackend,F=@@fixture@@,me=F.player;B.emulator.register(me.id,me.name);B.accounts[me.name.toLowerCase()]={id:me.id,username:me.name,password:me.password};B.invoke(me.id,'LudisseyBootstrap',{gender:me.gender});const profile=JSON.parse(B.emulator.players[me.id].readOnly.LudisseyProfile);profile.createdAt=me.createdAt;B.emulator.players[me.id].readOnly.LudisseyProfile=JSON.stringify(profile);const owners={};owners[me.name]=me.id;for(const f of F.friends){B.enroll(f.id,f.name);B.emulator.players[f.id].readOnly.LudisseyProfile=JSON.stringify({username:f.name,gender:f.gender,createdAt:me.createdAt});owners[f.name]=f.id;}for(const g of F.games)B.invoke(owners[g.author],'LudisseyPublishWorld',{world:{id:g.id,title:g.title,blocks:[B.baseplate(g.id)],genre:g.genre,serverSize:RULES.serverSize.standard}});B.emulator.title.LudisseyExperienceMetadata=JSON.stringify(Object.fromEntries(F.games.map(g=>[g.id,{description:g.description,featured:g.author===me.name,weeklyPlays:F.weeklyPlays}])));for(const f of F.friends){B.invoke(f.id,'LudisseyRequestFriend',{id:me.id});B.invoke(me.id,'LudisseyRespondFriend',{id:f.id,accept:true});if(f.online)B.emulator.players[f.id].user.LudisseyPresence=String(Date.now()/1000);}const look=standardAvatar(me.name,me.gender);Object.assign(look.bodyColors,me.bodyColors);B.invoke(me.id,'LudisseySaveAvatar',{avatar:look});document.getElementById('username').value=me.name;document.getElementById('password').value=me.password;document.getElementById('auth-form').dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}));for(let i=0;i<400&&!(stateReady&&linked());i++)await new Promise(r=>setTimeout(r,25));for(const e of catalogEntries){e.createdAt=null;e.updatedAt=null;}})().catch(e=>{window.ludisseyError=String(e&&e.message||e);}).finally(()=>{window.ludisseyDone=true;});'ok')JS";

}

TEST_CASE("Parità: sito e app, stesse schermate", "[web][parity][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Pmartina", sample::player), nullptr, false);
        sample::populate(*store);
        store->friends = sample::friends();
        ui::ClientView view(store);
        ui::Surface surface([&] { return view.build(); });
        view.invalidated = [&] { surface.invalidate(); };
        auto site = stageSite("Parità");
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        std::string prelude = "window.ludisseyLanguage='it';document.hasFocus=()=>true;document.addEventListener('DOMContentLoaded',()=>{const s=document.createElement('style');s.textContent='::-webkit-scrollbar{display:none}';document.head.append(s);});" + backendShim();
        [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:[NSString stringWithUTF8String:prelude.c_str()] injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        const int height = 760;
        WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1180, height) configuration:configuration];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:web.frame styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = web;
        [window orderFront:nil];
        LDParityLoad* delegate = [[LDParityLoad alloc] init];
        web.navigationDelegate = delegate;
        [web loadFileURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:(site / "index.html").c_str()]] allowingReadAccessToURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:site.c_str()]]];
        REQUIRE(pump(30, [&] { return delegate.finished == YES; }));
        std::string seeding = seed;
        seeding.replace(seeding.find("@@fixture@@"), 11, sample::fixture().dump());
        REQUIRE(evaluate(web, seeding) == "ok");
        REQUIRE(pump(30, [&] { return evaluate(web, "window.ludisseyDone?'1':''") == "1"; }));
        REQUIRE(evaluate(web, "window.ludisseyError") == "");
        REQUIRE(evaluate(web, "stateReady&&linked()&&state.username") == sample::player);
        REQUIRE(pump(20, [&] { return evaluate(web, "document.fonts.status==='loaded'&&document.fonts.check('16px \"Helvetica Neue\"')?'1':''") == "1"; }));
        std::string detail = sample::games().front().id;
        harness::awaitVoid(store->loadDetails(detail));
        struct Screen {
            std::string name, route;
            ld::Route native;
            bool detail;
        };
        std::vector<Screen> screens{{"home", "home", ld::Route::home, false}, {"esperienze", "experiences", ld::Route::experiences, false}, {"esperienza", "experience/" + detail, ld::Route::experiences, true}, {"avatar", "avatar", ld::Route::avatar, false}, {"amici", "friends", ld::Route::friends, false}, {"impostazioni", "settings", ld::Route::settings, false}};
        const char* shots = std::getenv("LUDISSEY_SHOTS");
        const std::vector<std::string> framed{"topNavigation", "topNavigation.logo", "topNavigation.home", "topNavigation.experiences", "topNavigation.avatar", "topNavigation.friends", "topNavigation.create", "utility.notifications", "topNavigation.settings", "topNavigation.profile", "home.headshot", "home.friends", "shelf.popular", "experience.cover", "experience.statistics", "avatar.preview.frame", "settings.page"};
        for (int width : {1180, 1400}) {
            [window setContentSize:NSMakeSize(width, height)];
            web.frame = NSMakeRect(0, 0, width, height);
            [web layoutSubtreeIfNeeded];
            for (const auto& screen : screens) {
                INFO(screen.name << " a " << width);
                store->detail.reset();
                store->navigate(screen.native);
                if (screen.detail) {
                    auto catalog = store->experienceCatalog();
                    auto found = std::find_if(catalog.begin(), catalog.end(), [&](const auto& e) { return e.id == detail; });
                    REQUIRE(found != catalog.end());
                    store->showExperience(*found);
                }
                surface.invalidate();
                surface.resize({static_cast<double>(width), static_cast<double>(height)}, 1);
                ui::Canvas canvas(width, height, 1);
                surface.render(canvas);
                surface.render(canvas);
                REQUIRE(evaluate(web, "route='" + screen.route + "';query='';listing=null;experienceTab='Informazioni';friendsTab=0;settingsTab=0;notificationsOpen=false;accountMenu=false;statusMessage='';clearInterval(pollTimer);document.documentElement.style.overflow='hidden';render();window.scrollTo(0,0);'ok'") == "ok");
                pump(0.35, [] { return false; });
                REQUIRE(evaluate(web, "statusMessage='';updateConnection();'ok'") == "ok");
                pixels::Pixels site = snapshot(web, width, height);
                pixels::Pixels native{width, height, std::vector<std::uint8_t>(canvas.pixels(), canvas.pixels() + static_cast<std::size_t>(width * height * 4))};
                REQUIRE(site.width == width);
                if (shots) {
                    std::filesystem::create_directories(shots);
                    pixels::save(site, std::filesystem::path(shots) / ("parità sito " + screen.name + " " + std::to_string(width) + ".png"));
                    pixels::save(native, std::filesystem::path(shots) / ("parità app " + screen.name + " " + std::to_string(width) + ".png"));
                }
                auto create = surface.find("topNavigation.create");
                auto bell = surface.find("utility.notifications");
                REQUIRE(create);
                REQUIRE(bell);
                for (int y = 0; y < 40; ++y)
                    for (int x = static_cast<int>(create->frame.maxX()); x < static_cast<int>(bell->frame.x); ++x)
                        for (int c = 0; c < 4; ++c) site.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)] = native.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)];
                if (auto preview = surface.find("avatar.preview.frame"))
                    for (int y = static_cast<int>(preview->frame.y); y < static_cast<int>(preview->frame.maxY()) && y < height; ++y)
                        for (int x = static_cast<int>(preview->frame.x); x < static_cast<int>(preview->frame.maxX()); ++x)
                            for (int c = 0; c < 4; ++c) site.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)] = native.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)];
                double bar = similarity(native, site, 0, 40), content = similarity(native, site, 40, height);
                WARN(screen.name << " " << width << ": barra " << bar << ", contenuto " << content);
                CHECK(bar >= 0.92);
                CHECK(content >= 0.92);
                auto rects = ld::Json::parse(evaluate(web, "JSON.stringify(Object.fromEntries([...document.querySelectorAll('[data-id]')].map(e=>{const r=e.getBoundingClientRect();return [e.dataset.id,[r.x,r.y,r.width,r.height]];})))"));
                REQUIRE(rects.ok());
                for (const auto& id : framed) {
                    auto element = surface.find(id);
                    const ld::Json* other = rects->find(id);
                    if (!element && !other) continue;
                    INFO(id);
                    REQUIRE(element);
                    REQUIRE(other);
                    CHECK(std::abs(element->frame.x - (*other)[0].number()) <= 2);
                    CHECK(std::abs(element->frame.y - (*other)[1].number()) <= 2);
                    CHECK(std::abs(element->frame.width - (*other)[2].number()) <= 2);
                    if (id != "settings.page") CHECK(std::abs(element->frame.height - (*other)[3].number()) <= 2);
                }
            }
        }
        [window close];
        std::error_code ignored;
        std::filesystem::remove_all(site, ignored);
    }
}

TEST_CASE("Parità: accesso su sito e app", "[web][parity][screens]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        golden::registry();
        harness::Fixture fixture;
        auto store = std::make_shared<ld::Store>(std::make_shared<ld::PlayFab>(std::make_shared<ld::MemoryStore>()), fixture.world->registerPlayer("Paccesso", sample::player), nullptr, false);
        ui::ClientView view(store);
        ui::Surface surface([&] { return view.build(); });
        auto site = stageSite("Accesso");
        WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
        configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
        [configuration.userContentController addUserScript:[[WKUserScript alloc] initWithSource:@"window.ludisseyLanguage='it';" injectionTime:WKUserScriptInjectionTimeAtDocumentStart forMainFrameOnly:YES]];
        const int height = 760;
        WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1180, height) configuration:configuration];
        NSWindow* window = [[NSWindow alloc] initWithContentRect:web.frame styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = web;
        [window orderFront:nil];
        LDParityLoad* delegate = [[LDParityLoad alloc] init];
        web.navigationDelegate = delegate;
        [web loadFileURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:(site / "index.html").c_str()]] allowingReadAccessToURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:site.c_str()]]];
        REQUIRE(pump(30, [&] { return delegate.finished == YES; }));
        REQUIRE(pump(20, [&] { return evaluate(web, "document.fonts.status==='loaded'&&document.fonts.check('16px \"Helvetica Neue\"')&&!!document.querySelector('#register-form')?'1':''") == "1"; }));
        const char* shots = std::getenv("LUDISSEY_SHOTS");
        for (int width : {1180, 1400}) {
            INFO("accesso a " << width);
            [window setContentSize:NSMakeSize(width, height)];
            web.frame = NSMakeRect(0, 0, width, height);
            [web layoutSubtreeIfNeeded];
            surface.invalidate();
            surface.resize({static_cast<double>(width), static_cast<double>(height)}, 1);
            ui::Canvas canvas(width, height, 1);
            surface.render(canvas);
            surface.render(canvas);
            REQUIRE(evaluate(web, "document.documentElement.style.overflow='hidden';document.querySelector('.authwrap').style.minHeight='" + std::to_string(height) + "px';document.querySelector('.authwrap').style.height='" + std::to_string(height) + "px';document.querySelectorAll('input').forEach(i=>i.removeAttribute('placeholder'));window.scrollTo(0,0);'ok'") == "ok");
            pump(0.35, [] { return false; });
            pixels::Pixels page = snapshot(web, width, height);
            pixels::Pixels native{width, height, std::vector<std::uint8_t>(canvas.pixels(), canvas.pixels() + static_cast<std::size_t>(width * height * 4))};
            REQUIRE(page.width == width);
            if (shots) {
                std::filesystem::create_directories(shots);
                pixels::save(page, std::filesystem::path(shots) / ("parità sito accesso " + std::to_string(width) + ".png"));
                pixels::save(native, std::filesystem::path(shots) / ("parità app accesso " + std::to_string(width) + ".png"));
            }
            auto rects = ld::Json::parse(evaluate(web, "JSON.stringify(Object.fromEntries([...document.querySelectorAll('[data-id]')].map(e=>{const r=e.getBoundingClientRect();return [e.dataset.id,[r.x,r.y,r.width,r.height]];}).concat([['download',(r=>[r.x,r.y,r.width,r.height])(document.querySelector('.loginbar .download').getBoundingClientRect())]])))"));
            REQUIRE(rects.ok());
            const ld::Json& download = (*rects)["download"];
            for (int y = 0; y < 56; ++y)
                for (int x = static_cast<int>(download[0].number()) - 2; x < static_cast<int>(download[0].number() + download[2].number()) + 2; ++x)
                    for (int c = 0; c < 4; ++c) page.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)] = native.rgba[static_cast<std::size_t>((y * width + x) * 4 + c)];
            double bar = similarity(native, page, 0, 56), content = similarity(native, page, 56, height);
            WARN("accesso " << width << ": barra " << bar << ", contenuto " << content);
            CHECK(bar >= 0.92);
            CHECK(content >= 0.92);
            for (const std::string id : {"login.bar", "login.card", "login.brand"}) {
                INFO(id);
                auto element = surface.find(id);
                const ld::Json* other = rects->find(id);
                REQUIRE(element);
                REQUIRE(other);
                WARN(id << " app " << element->frame.x << " " << element->frame.y << " " << element->frame.width << " " << element->frame.height << " sito " << (*other)[0].number() << " " << (*other)[1].number() << " " << (*other)[2].number() << " " << (*other)[3].number());
                CHECK(std::abs(element->frame.x - (*other)[0].number()) <= 2);
                CHECK(std::abs(element->frame.y - (*other)[1].number()) <= 2);
                CHECK(std::abs(element->frame.width - (*other)[2].number()) <= 2);
                CHECK(std::abs(element->frame.height - (*other)[3].number()) <= 2);
            }
        }
        [window close];
        std::error_code ignored;
        std::filesystem::remove_all(site, ignored);
    }
}
