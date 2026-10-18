#include "Components.hpp"

#include "Engine/Core/Resources.hpp"
#include "Engine/Core/Rules.hpp"
#include "Interface/Glyphs.hpp"
#include "Interface/Surface.hpp"
#include "Localization/Localization.hpp"

#include <array>
#include <cmath>
#include <map>
#include <mutex>

namespace ui {

namespace theme {

std::filesystem::path resources() { return ld::resourceFolder(); }

namespace {

Scale active = Scale::client;

}

void applyScale(Scale value) {
    active = value;
    Typography fonts = typography(value);
    type::dialogTitle = fonts.dialogTitle;
    type::heading = fonts.heading;
    type::pageTitle = fonts.pageTitle;
    type::display = fonts.display;
    type::body = fonts.body;
    type::strong = fonts.strong;
    type::sectionTitle = fonts.sectionTitle;
    type::small = fonts.small;
    type::smallStrong = fonts.smallStrong;
    type::caption = fonts.caption;
    type::micro = fonts.micro;
    type::code = fonts.code;
    type::label = fonts.label;
    type::barTitle = fonts.barTitle;
}

Scale scale() { return active; }

}

namespace icons {

namespace {

std::mutex lock;

}

bool packed(const std::string& name) { return semanticFiles().count(name) > 0; }

std::string file(const std::string& name) {
    auto it = semanticFiles().find(name);
    return it == semanticFiles().end() ? "" : it->second;
}

std::string placeholder(const std::string& name) {
    auto it = placeholders().find(name);
    return it == placeholders().end() ? "" : it->second;
}

std::filesystem::path folder() {
    auto base = theme::resources() / "Icons";
    return std::filesystem::exists(base / "Pack") ? base / "Pack" : base;
}

Image load(const std::string& name) {
    std::string f = file(name);
    if (f.empty()) return {};
    static std::map<std::string, Image> cache;
    std::string key = theme::resources().string() + "\n" + f;
    std::lock_guard<std::mutex> guard(lock);
    auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    Image made = loadImage(folder() / f);
    if (made.valid()) cache[key] = made;
    return made;
}

Image sized(const std::string& name, int pixels) {
    Image master = load(name);
    if (!master.valid() || pixels <= 0) return {};
    pixels = std::min({pixels, master.pixelsWide, master.pixelsHigh});
    static std::map<std::pair<std::string, int>, Image> cache;
    std::lock_guard<std::mutex> guard(lock);
    auto key = std::make_pair(theme::resources().string() + "\n" + name, pixels);
    if (auto found = cache.find(key); found != cache.end()) return found->second;
    Image made = pixels == master.pixelsWide ? master : resampled(master, pixels, pixels);
    if (made.valid()) cache[key] = made;
    return made;
}

}

using namespace theme;

namespace {

const glyphs::Glyph* findGlyph(const std::string& name) {
    for (const auto& entry : glyphs::all)
        if (entry.name == name) return &entry;
    return nullptr;
}

Font controlFont(const Environment& env) { return env.compact && theme::scale() == Scale::client ? type::small : type::body; }

}

NodePtr classicBorder(NodePtr content, double radius, bool strong) { return content->overlay(strokeRectangle(strong ? colors::borderStrong : colors::border, borders::pixels, radius)); }

NodePtr plainButton(std::string id, NodePtr label, std::function<void()> action, Cursor cursor) { return label->onTap(std::move(id), std::move(action), cursor); }

namespace {

Gradient glossGradient(const Gloss& g) { return Gradient({{g.top, 0}, {g.mid, 0.5}, {g.split, 0.5}, {g.bottom, 1}}); }

Gradient sheenGradient(const Sheen& s) { return Gradient({{s.top, 0}, {s.bottom, 1}}); }

const Gloss& glossFor(ButtonRole role) {
    switch (role) {
    case ButtonRole::primary: return gloss::purple;
    case ButtonRole::success: return gloss::green;
    case ButtonRole::destructive: return gloss::red;
    case ButtonRole::dark: return gloss::black;
    case ButtonRole::warning: return gloss::purple;
    case ButtonRole::secondary: break;
    }
    return gloss::white;
}

void paintGloss(Canvas& c, const Frame& f, const Gloss& g, double radius, bool sunk) {
    c.fillGradient(f, glossGradient(g), radius);
    if (sunk) c.innerShadow(f, radius, drops::pressed.dy, drops::pressed.blur, drops::pressed.color);
    else c.innerShadow(f, radius, drops::highlight.dy, drops::highlight.blur, drops::highlight.color);
    c.stroke(f, g.border, borders::pixels, radius);
}

NodePtr glossFace(const Gloss& g, double radius, bool sunk) {
    return canvas([g, radius, sunk](Canvas& c, const Frame& f) { paintGloss(c, f, g, radius, sunk); });
}

}

NodePtr embossed(NodePtr node, const Emboss& relief) { return node->shadow(relief.color, 0, relief.dy, 0); }

namespace {

NodePtr relief(NodePtr node, const Emboss& kind, bool disabled) { return disabled ? node : embossed(std::move(node), kind); }

}

NodePtr button(std::string id, NodePtr label, std::function<void()> action, ButtonRole role, ButtonSize size) {
    return reader([id, label, action, role, size](const Environment& env) {
        Surface* s = Surface::active();
        bool pressed = s && s->pressed(id);
        ButtonSize shape = env.compact && size == ButtonSize::normal ? ButtonSize::small : size;
        double height = shape == ButtonSize::small ? metrics::buttonSmall : shape == ButtonSize::large ? metrics::buttonLarge : metrics::button;
        double radius = shape == ButtonSize::small ? metrics::barRadius : shape == ButtonSize::large ? metrics::buttonLargeRadius : metrics::buttonRadius;
        double points = shape == ButtonSize::small ? tokens::font::buttonSmall : shape == ButtonSize::large ? tokens::font::action : tokens::font::button;
        const Gloss& g = glossFor(role);
        bool light = role == ButtonRole::secondary;
        Font face{std::string(tokens::font::family), points, Weight::bold};
        Color ink = !light ? colors::textOnAccent : env.disabled ? colors::textDisabled : colors::link;
        auto content = relief(label->font(face)->foreground(ink), light ? emboss::light : emboss::dark, env.disabled)
                           ->paddingHorizontal(shape == ButtonSize::large ? spacing::s12 : shape == ButtonSize::small ? spacing::s6 : spacing::s8)
                           ->flexible(std::nullopt, std::nullopt, height, height);
        auto tapped = content->background(glossFace(g, radius, pressed))->opacity(env.disabled ? tokens::opacity::disabled : 1)->onTap(id, action, Cursor::pointer);
        tapped->label = label->label;
        return tapped;
    });
}

NodePtr button(std::string id, std::string title, std::function<void()> action, ButtonRole role, ButtonSize size) {
    auto face = lineLimited(text(title), 1)->fixed(true, false);
    face->label = title;
    return button(std::move(id), face, std::move(action), role, size);
}

NodePtr playButton(std::string id, std::string title, std::function<void()> action) {
    return reader([id, title, action](const Environment& env) {
        Surface* s = Surface::active();
        bool pressed = s && s->pressed(id);
        Font face{std::string(tokens::font::family), tokens::font::play, Weight::bold};
        auto label = relief(hstack({glyph("gioca", iconSize::large, colors::textOnAccent), text(title, face, colors::textOnAccent)}, spacing::s4), emboss::dark, env.disabled);
        return label->flexible(std::nullopt, infinity, metrics::playButton, metrics::playButton)
            ->background(glossFace(gloss::green, metrics::buttonLargeRadius, pressed))
            ->opacity(env.disabled ? tokens::opacity::disabled : 1)
            ->onTap(id, action, Cursor::pointer);
    });
}

NodePtr linkButton(std::string id, std::string title, std::function<void()> action) { return button(std::move(id), std::move(title), std::move(action), ButtonRole::primary, ButtonSize::small); }

NodePtr backButton(std::string id, std::string title, std::function<void()> action) {
    return reader([id, title, action](const Environment& env) {
        Surface* s = Surface::active();
        bool pressed = s && s->pressed(id);
        Font face{std::string(tokens::font::family), tokens::font::backButton, Weight::bold};
        auto label = relief(text(title, face, env.disabled ? colors::textDisabled : colors::link), emboss::light, env.disabled)->padding(Insets{0, metrics::backArrow + spacing::s4, 0, spacing::s8})->flexible(std::nullopt, std::nullopt, metrics::barButton, metrics::barButton);
        auto shape = canvas([pressed](Canvas& c, const Frame& f) {
            const Gloss& g = gloss::white;
            Gradient fill = pressed ? Gradient({{g.bottom, 0}, {g.split, 1}}) : Gradient({{g.top, 0}, {g.mid, 0.5}, {g.split, 0.5}, {g.bottom, 1}});
            c.arrowShape(f, metrics::backArrow, metrics::barRadius, fill, g.border, Color::white(tokens::opacity::buttonLight));
        });
        auto tapped = label->background(shape)->opacity(env.disabled ? tokens::opacity::disabled : 1)->onTap(id, action, Cursor::pointer);
        tapped->label = title;
        return tapped;
    });
}

NodePtr barButton(std::string id, NodePtr content, std::function<void()> action, bool on) {
    return reader([id, content, action, on](const Environment& env) {
        Surface* s = Surface::active();
        bool sunk = on || (s && s->pressed(id));
        Font face{std::string(tokens::font::family), tokens::font::buttonSmall, Weight::bold};
        auto inside = relief(content->font(face)->foreground(colors::textOnAccent), emboss::dark, env.disabled)->paddingHorizontal(spacing::s4)->flexible(metrics::barButton, std::nullopt, metrics::barButton, metrics::barButton);
        return inside->background(glossFace(sunk ? gloss::barButtonOn : gloss::barButton, metrics::barRadius, sunk))->opacity(env.disabled ? tokens::opacity::disabled : 1)->onTap(id, action, Cursor::pointer);
    });
}

NodePtr styledField(NativeField field) {
    return reader([field](const Environment& env) mutable {
        if (field.font == Font{}) field.font = env.compact ? type::small : type::body;
        if (field.color == Color{}) field.color = colors::textPrimary;
        double height = env.compact ? metrics::compactControlHeight : metrics::textField;
        double radius = env.compact ? metrics::controlRadius : metrics::fieldRadius;
        Surface* s = Surface::active();
        bool focused = s && s->focused(field.id);
        auto well = canvas([radius, focused](Canvas& c, const Frame& f) {
            c.fill(f, colors::surface, radius);
            c.innerShadow(f, radius, drops::field.dy, drops::field.blur, drops::field.color);
            c.stroke(f, focused ? colors::focus : colors::border, borders::pixels, radius);
        });
        return textField(field)->paddingHorizontal(env.compact ? spacing::s3 : spacing::s4)->flexible(std::nullopt, std::nullopt, height, std::nullopt)->background(well);
    });
}

NodePtr select(NativeField field) {
    return reader([field](const Environment& env) mutable {
        field.font = Font{std::string(tokens::font::family), env.compact ? tokens::studioFont::small : tokens::font::small, Weight::bold};
        double height = env.compact ? metrics::compactControlHeight : metrics::buttonSmall;
        std::string shown = field.selectedOption >= 0 && field.selectedOption < static_cast<int>(field.options.size()) ? field.options[static_cast<std::size_t>(field.selectedOption)] : field.text;
        Color ink = env.disabled || !field.editable ? colors::textDisabled : colors::textPrimary;
        auto tip = vstack({glyph("freccia-su", tokens::icon::chevron, colors::textOnAccent), glyph("freccia-giu", tokens::icon::chevron, colors::textOnAccent)}, -spacing::s2)
                       ->frameSize(metrics::popupTip, height)
                       ->background(canvas([](Canvas& c, const Frame& f) {
                           c.fillGradient(f, glossGradient(gloss::purple));
                           c.line({f.x + 0.5, f.y}, {f.x + 0.5, f.maxY()}, gloss::purple.border, borders::pixels);
                       }));
        auto face = hstack({relief(lineLimited(text(shown, field.font, ink), 1), emboss::light, env.disabled || !field.editable)->paddingHorizontal(spacing::s6), spacer(0.0), tip}, 0)->flexible(std::nullopt, std::nullopt, height, height);
        auto frame = canvas([](Canvas& c, const Frame& f) { c.fillGradient(f, glossGradient(gloss::white), metrics::buttonRadius); });
        return zstack({face, popup(field)})->background(frame)->clipped(metrics::buttonRadius)->overlay(strokeRectangle(gloss::white.border, borders::pixels, metrics::buttonRadius))->opacity(env.disabled ? tokens::opacity::disabled : 1);
    });
}

NodePtr glossBackground(ButtonRole role, double radius, bool sunk) { return glossFace(glossFor(role), radius, sunk); }

NodePtr segments(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose, bool small, const std::vector<std::string>& keys) {
    return reader([id, titles, selected, choose, small, keys](const Environment& env) {
        double height = small || env.compact ? metrics::segmentedSmall : metrics::segmented;
        Font face{std::string(tokens::font::family), small || env.compact ? tokens::font::buttonSmall : tokens::font::small, Weight::bold};
        std::vector<NodePtr> items;
        for (std::size_t i = 0; i < titles.size(); ++i) {
            bool on = static_cast<int>(i) == selected;
            std::string key = id + "." + (i < keys.size() ? keys[i] : std::to_string(i));
            auto background = canvas([on](Canvas& c, const Frame& f) {
                c.fillGradient(f, glossGradient(on ? gloss::segmentOn : gloss::segment));
                if (on) c.innerShadow(f, 0, drops::pressed.dy, drops::pressed.blur, drops::pressed.color);
            });
            Color ink = on ? colors::textOnAccent : env.disabled ? colors::textDisabled : colors::textSecondary;
            auto label = relief(lineLimited(text(titles[i], face, ink), 1), on ? emboss::dark : emboss::light, env.disabled)->paddingHorizontal(spacing::s8)->flexible(std::nullopt, std::nullopt, height, height)->background(background);
            int index = static_cast<int>(i);
            auto item = plainButton(key, label, [choose, index] { choose(index); }, Cursor::pointer);
            item->selected = on;
            item->label = titles[i];
            if (i > 0) items.push_back(rectangle(gloss::segment.border)->frameSize(borders::pixels, height));
            items.push_back(item);
        }
        return hstack(std::move(items), 0)->fixed(true, true)->clipped(metrics::segmentedRadius)->overlay(strokeRectangle(gloss::segment.border, borders::pixels, metrics::segmentedRadius))->opacity(env.disabled ? tokens::opacity::disabled : 1)->id(id);
    });
}

NodePtr pageTabs(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose) { return segments(std::move(id), titles, selected, std::move(choose)); }

NodePtr classicTabs(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose) { return segments(std::move(id), titles, selected, std::move(choose), true); }

NodePtr toggle(std::string id, bool on, std::function<void(bool)> change) {
    return reader([id, on, change](const Environment& env) {
        double scale = env.compact ? tokens::studioFont::body / tokens::font::body : 1;
        double width = metrics::toggleWidth * scale, height = metrics::toggleHeight * scale, knob = metrics::switchKnob * scale, radius = metrics::switchRadius * scale;
        Font face{std::string(tokens::font::family), tokens::font::body * scale, Weight::bold};
        auto track = canvas([on, knob, radius](Canvas& c, const Frame& f) {
            const Sheen& s = on ? sheen::switchOn : sheen::switchOff;
            c.fillGradient(f, Gradient(on ? s.top : s.top, on ? s.bottom : s.bottom), radius);
            c.innerShadow(f, radius, drops::pressed.dy * 2, drops::pressed.blur, on ? drops::pressed.color : Color::black(drops::field.color.a));
            Frame k{on ? f.maxX() - knob : f.x, f.y, knob, f.height};
            c.fillGradient(k, glossGradient(gloss::knob), radius - borders::pixels);
            c.stroke(k, gloss::knob.border, borders::pixels, radius - borders::pixels);
            c.stroke(f, s.border, borders::pixels, radius);
        });
        auto word = on ? relief(text(ld::tr("SÌ"), face, colors::textOnAccent), emboss::dark, env.disabled) : relief(text(ld::tr("NO"), face, env.disabled ? colors::textDisabled : colors::textTertiary), emboss::light, env.disabled);
        auto label = hstack(on ? std::vector<NodePtr>{word->frameSize(width - knob, height), spacer(0.0)} : std::vector<NodePtr>{spacer(0.0), word->frameSize(width - knob, height)}, 0);
        auto result = plainButton(id, label->frameSize(width, height)->background(track)->opacity(env.disabled ? tokens::opacity::disabled : 1), [change, on] { change(!on); }, Cursor::pointer);
        result->label = on ? ld::tr("Attivo") : ld::tr("Disattivato");
        result->selected = on;
        return result;
    });
}

NodePtr radio(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose) {
    return reader([id, titles, selected, choose](const Environment& env) {
        std::vector<NodePtr> rows;
        for (std::size_t i = 0; i < titles.size(); ++i) {
            bool on = static_cast<int>(i) == selected;
            std::string key = id + "." + std::to_string(i);
            Surface* s = Surface::active();
            bool hovered = s && s->hovered(key);
            auto mark = glyph("spunta", iconSize::compact, on ? colors::accent : Color::clear());
            auto line = hstack({lineLimited(text(titles[i], env.compact ? type::small : type::body, on ? colors::accent : colors::textPrimary), 1), spacer(spacing::s4), mark}, spacing::s2)
                            ->paddingHorizontal(spacing::s4)
                            ->frameSize(std::nullopt, env.compact ? metrics::explorerRow : metrics::listRow)
                            ->fillWidth(Alignment::leading())
                            ->background(rectangle(hovered ? colors::rowAlternate : colors::surface));
            int index = static_cast<int>(i);
            auto row = plainButton(key, line, [choose, index] { choose(index); }, Cursor::pointer);
            row->selected = on;
            row->label = titles[i];
            if (i > 0) rows.push_back(rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels));
            rows.push_back(row);
        }
        double radius = env.compact ? metrics::controlRadius : metrics::groupRadius;
        return vstack(std::move(rows), 0, HAlign::leading)->clipped(radius)->overlay(strokeRectangle(colors::border, borders::pixels, radius))->id(id);
    });
}

NodePtr slider(std::string id, double value, double low, double high, double step, Color accent, std::function<void(double)> change, Color) {
    double fraction = high > low ? std::clamp((value - low) / (high - low), 0.0, 1.0) : 0;
    auto track = canvas([fraction, accent](Canvas& c, const Frame& f) {
        double knob = metrics::slider, rail = metrics::sliderTrack;
        double left = f.x + knob / 2, right = f.maxX() - knob / 2, x = left + (right - left) * fraction;
        Frame bed{f.x, f.midY() - rail / 2, f.width, rail};
        c.fillGradient(bed, sheenGradient(sheen::track), rail / 2);
        c.fill({bed.x, bed.y, x - bed.x, rail}, accent, rail / 2);
        c.innerShadow(bed, rail / 2, drops::field.dy, drops::pressed.blur, drops::pressed.color);
        c.stroke(bed, Color::black(tokens::opacity::blackShade), borders::pixels, rail / 2);
        Frame k{x - knob / 2, f.midY() - knob / 2, knob, knob};
        c.boxShadow(k, knob / 2, 0, drops::field.dy, drops::field.blur, drops::button.color);
        c.fillGradient(k, glossGradient(gloss::knob), knob / 2);
        c.stroke(k, gloss::knob.border, borders::pixels, knob / 2);
    }, Size{100, metrics::slider});
    auto update = [low, high, step, change](double x, double width) {
        if (width <= 0) return;
        double knob = metrics::slider;
        double t = std::clamp((x - knob / 2) / std::max(1.0, width - knob), 0.0, 1.0);
        double raw = low + (high - low) * t;
        double snapped = step > 0 ? low + std::round((raw - low) / step) * step : raw;
        change(std::clamp(snapped, low, high));
    };
    auto target = track->frameSize(std::nullopt, metrics::slider)->fillWidth()->onDrag(id, [update](const DragEvent& e) { update(e.location.x - e.bounds.x, e.bounds.width); });
    Node* area = target.get();
    target->interaction.press = [update, area](Point p) { update(p.x, area->frame.width); };
    return target;
}

NodePtr group(std::vector<NodePtr> rows) {
    std::vector<NodePtr> items;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) items.push_back(rectangle(colors::borderSubtle)->frameSize(std::nullopt, borders::pixels)->fillWidth());
        items.push_back(rows[i]->fillWidth(Alignment::leading()));
    }
    return vstack(std::move(items), 0, HAlign::leading)->background(rectangle(colors::surface))->clipped(metrics::groupRadius)->overlay(strokeRectangle(colors::border, borders::pixels, metrics::groupRadius))->background(roundedRectangle(metrics::groupRadius, drops::group.color)->offset(0, drops::group.dy));
}

NodePtr groupRow(NodePtr label, NodePtr accessory, std::string detail) {
    std::vector<NodePtr> words{label->font(type::label)->foreground(colors::textPrimary)};
    if (!detail.empty()) words.push_back(text(detail, type::small, colors::textTertiary)->fixed(false, true));
    std::vector<NodePtr> items{vstack(std::move(words), spacing::s1, HAlign::leading)->fixed(false, true), spacer(spacing::s6)};
    if (accessory) items.push_back(accessory);
    return hstack(std::move(items), spacing::s4)->paddingHorizontal(spacing::s6)->paddingVertical(spacing::s3)->flexible(std::nullopt, infinity, metrics::listRow, std::nullopt, Alignment::leading());
}

NodePtr disclosure(std::string value) {
    std::vector<NodePtr> items;
    if (!value.empty()) items.push_back(text(std::move(value), type::body, colors::value));
    items.push_back(glyph("freccia-destra", iconSize::compact, colors::textTertiary));
    return hstack(std::move(items), spacing::s3);
}

NodePtr groupTitle(const std::string& title) { return embossed(text(title, type::label, colors::textSecondary), emboss::light)->padding(Insets{0, spacing::s6, spacing::s3, 0})->fillWidth(Alignment::leading()); }

NodePtr groupNote(const std::string& note) { return embossed(text(note, type::small, colors::textTertiary)->multilineAlignment(TextAlign::center)->fixed(false, true), emboss::light)->padding(Insets{spacing::s3, spacing::s6, 0, spacing::s6})->fillWidth(); }

NodePtr card(NodePtr content, const std::string&) { return content->background(rectangle(colors::surface))->clipped(metrics::groupRadius)->overlay(strokeRectangle(colors::border, borders::pixels, metrics::groupRadius))->background(roundedRectangle(metrics::groupRadius, drops::group.color)->offset(0, drops::group.dy)); }

NodePtr counter(int count, bool warning) {
    Font face{std::string(tokens::font::family), tokens::font::buttonSmall, Weight::bold};
    double side = metrics::badge;
    auto badge = canvas([side, warning](Canvas& c, const Frame& f) {
        const auto& tone = warning ? gloss::badgeWarning : gloss::badge;
        c.boxShadow(f, side / 2, 0, drops::field.dy, drops::bar.blur, drops::button.color);
        c.fillGradient(f, glossGradient(tone), side / 2);
        c.stroke(f, tone.border, borders::focus, side / 2);
    });
    return embossed(text(count > 99 ? "99+" : std::to_string(count), face, colors::textOnAccent), emboss::dark)->paddingHorizontal(spacing::s3)->flexible(side, std::nullopt, side, side)->background(badge);
}

NodePtr presence(bool online, double side) {
    Color base = online ? colors::online : colors::borderStrong;
    return canvas([base](Canvas& c, const Frame& f) {
        c.boxShadow(f, f.width / 2, 0, drops::field.dy, drops::field.blur, drops::button.color);
        c.fillEllipse(f, colors::surface);
        Frame core = f.inset(borders::focus, borders::focus);
        c.fillEllipse(core, base);
        c.fillEllipse({core.x + core.width * 0.2, core.y + core.height * 0.12, core.width * 0.5, core.height * 0.42}, Color::white(tokens::opacity::dotHighlight));
    })->frameSize(side, side);
}

NodePtr unreadDot(double side) {
    return canvas([](Canvas& c, const Frame& f) {
        c.fillGradient(f, glossGradient(gloss::purple), f.width / 2);
        c.stroke(f, gloss::purple.border, borders::pixels, f.width / 2);
    })->frameSize(side, side);
}

NodePtr checkBadge(double side) {
    auto disc = canvas([](Canvas& c, const Frame& f) {
        c.boxShadow(f, f.width / 2, 0, drops::field.dy, drops::field.blur, drops::button.color);
        c.fillGradient(f, glossGradient(gloss::purple), f.width / 2);
        c.stroke(f, colors::surface, borders::focus, f.width / 2);
    });
    return glyph("spunta", side * 0.6, colors::textOnAccent)->frameSize(side, side)->background(disc);
}

NodePtr emptyState(const std::string& iconName, const std::string& sentence, NodePtr action) {
    std::vector<NodePtr> items{icon(iconName, iconSize::empty), text(sentence, type::body, colors::textSecondary)->fixed(false, true), spacer(0.0)};
    if (action) items.push_back(action);
    auto node = card(hstack(std::move(items), spacing::s6)->padding(spacing::s8)->fillWidth(Alignment::leading()));
    node->label = sentence;
    return node;
}

NodePtr dialog(const std::string& title, const std::string& message, std::vector<DialogButton> buttons, bool warning, NodePtr art) {
    Font heading{std::string(tokens::font::family), tokens::font::heading, Weight::bold};
    Font body{std::string(tokens::font::family), tokens::font::label, Weight::regular};
    Font press{std::string(tokens::font::family), tokens::font::alertButton, Weight::bold};
    std::vector<NodePtr> lines;
    if (art) lines.push_back(art);
    else if (warning) lines.push_back(icon("ld-icon-status-warning", tokens::icon::alert));
    if (!title.empty()) lines.push_back(embossed(text(title, heading, colors::textOnAccent)->multilineAlignment(TextAlign::center)->fixed(false, true), emboss::black));
    if (!message.empty()) lines.push_back(embossed(text(message, body, colors::textOnAccent)->multilineAlignment(TextAlign::center)->fixed(false, true), emboss::black));
    std::vector<NodePtr> row;
    int index = 0;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const auto& b = buttons[i];
        bool preferred = i + 1 == buttons.size() && buttons.size() > 1;
        auto face = canvas([preferred](Canvas& c, const Frame& f) {
            Gradient fill = preferred ? Gradient({{Color::white(tokens::opacity::alertDefaultTop), 0}, {Color::white(tokens::opacity::alertDefaultMid), 0.5}, {Color::white(tokens::opacity::alertDefaultSplit), 0.5}, {Color::white(tokens::opacity::alertDefaultBottom), 1}})
                                      : Gradient({{Color::white(tokens::opacity::alertButtonTop), 0}, {Color::white(tokens::opacity::alertButtonMid), 0.5}, {Color::white(tokens::opacity::alertButtonSplit), 0.5}, {Color::white(tokens::opacity::alertButtonBottom), 1}});
            c.fillGradient(f, fill, metrics::buttonRadius);
            c.innerShadow(f, metrics::buttonRadius, drops::highlight.dy, 0, Color::white(tokens::opacity::shineTop));
            c.stroke(f, Color::black(tokens::opacity::alertButtonEdge), borders::pixels, metrics::buttonRadius);
        });
        auto label = embossed(lineLimited(text(b.label, press, colors::textOnAccent), 1), emboss::black)->flexible(std::nullopt, infinity, metrics::alertButton, metrics::alertButton)->background(face);
        std::string key = b.id.empty() ? "dialog.button." + std::to_string(index) : b.id;
        ++index;
        auto pressable = plainButton(key, label, b.action, Cursor::pointer);
        pressable->label = b.label;
        row.push_back(pressable);
    }
    bool stacked = row.size() > 2;
    lines.push_back((stacked ? vstack(std::move(row), spacing::s3) : hstack(std::move(row), spacing::s4))->padding(Insets{spacing::s4, 0, 0, 0}));
    auto shine = canvas([](Canvas& c, const Frame& f) { c.shine(f, metrics::alertRadius, {f.x - f.width * 0.4, f.y - spacing::s16 * 2, f.width * 1.8, spacing::s16 * 4}, tokens::opacity::shineTop, tokens::opacity::shineBottom); });
    auto panel = vstack(std::move(lines), spacing::s4, HAlign::center)->padding(Insets{spacing::s8, spacing::s6, spacing::s6, spacing::s6})->frameSize(metrics::alertWidth, std::nullopt);
    auto sheet = panel->background(shine)->background(rectangle(colors::alertFill))->clipped(metrics::alertRadius)->overlay(strokeRectangle(Color::white(tokens::opacity::alertEdge), borders::focus, metrics::alertRadius))->boxShadow(drops::alert.color, 0, drops::alert.dy, drops::alert.blur, metrics::alertRadius);
    auto blocker = rectangle(Color::black(shadows::scrim))->onTap("dialog.scrim", [] {});
    return zstack({blocker, sheet})->foreground(colors::textOnAccent)->id("dialog");
}

NodePtr popover(NodePtr content, double width, const std::string& title, NodePtr accessory, double arrow) {
    Font heading{std::string(tokens::font::family), tokens::font::label, Weight::bold};
    std::vector<NodePtr> header{embossed(text(title, heading, colors::textOnAccent), emboss::black), spacer(0.0)};
    if (accessory) header.push_back(accessory);
    auto top = hstack(std::move(header), spacing::s4)->paddingHorizontal(spacing::s6)->frameSize(std::nullopt, metrics::popoverHeader);
    auto body = content->background(rectangle(colors::surface))->clipped(metrics::buttonRadius - borders::pixels);
    auto inner = vstack({title.empty() ? spacer(spacing::s3) : top, body}, 0, HAlign::leading)->padding(Insets{0, spacing::s3, spacing::s3, spacing::s3})->frameSize(width, std::nullopt);
    auto frame = canvas([](Canvas& c, const Frame& f) {
        c.fillGradient(f, Gradient({{sheen::popover.top, 0}, {Color::hex(tokens::gloss::barButtonOn::bottom), metrics::popoverHeader / std::max(f.height, 1.0)}, {sheen::popover.bottom, 1}}), metrics::popoverRadius);
        c.innerShadow(f, metrics::popoverRadius, drops::highlight.dy, 0, Color::white(tokens::opacity::shineTop));
        c.stroke(f, sheen::popover.border, borders::pixels, metrics::popoverRadius);
    });
    auto sheet = inner->background(frame)->boxShadow(drops::popover.color, 0, drops::popover.dy, drops::popover.blur, metrics::popoverRadius);
    double side = metrics::popoverArrow * 2;
    auto pointer = canvas([](Canvas& c, const Frame& f) {
        c.polygons({{{f.midX(), f.y}, {f.maxX(), f.maxY()}, {f.x, f.maxY()}}}, sheen::popover.top, false);
        c.line({f.midX(), f.y}, {f.x, f.maxY()}, sheen::popover.border, borders::pixels);
        c.line({f.midX(), f.y}, {f.maxX(), f.maxY()}, sheen::popover.border, borders::pixels);
    })->frameSize(side, metrics::popoverArrow);
    double inset = arrow < 0 ? width + arrow - side / 2 : arrow - side / 2;
    auto tip = hstack({spacer(inset), pointer, spacer(0.0)}, 0)->frameSize(width, metrics::popoverArrow);
    return vstack({tip, sheet->offset(0, -borders::pixels)}, 0, HAlign::leading);
}

NodePtr menuRow(std::string id, NodePtr label, std::function<void()> action) {
    Surface* s = Surface::active();
    bool hovered = s && s->hovered(id);
    auto face = label->font(type::label)->foreground(hovered ? colors::textOnAccent : colors::textPrimary)->paddingHorizontal(spacing::s6)->frameSize(std::nullopt, metrics::listRow)->fillWidth(Alignment::leading());
    return plainButton(id, face->background(hovered ? gradient(sheenGradient(sheen::selection)) : rectangle(Color::clear())), std::move(action), Cursor::pointer);
}

NodePtr appIcon(Image picture, const std::string& genre, double side) {
    double radius = side * metrics::appIconRadius;
    NodePtr face = picture.valid() ? ui::picture(picture, true) : genreTile(genre);
    auto shine = canvas([radius](Canvas& c, const Frame& f) { c.gloss(f, radius, tokens::opacity::iconTop, tokens::opacity::iconBottom); });
    return zstack({face, shine})->frameSize(side, side)->clipped(radius)->overlay(strokeRectangle(Color::hex(tokens::shadow::appIcon::color, tokens::opacity::iconEdge), borders::pixels, radius))->boxShadow(drops::appIcon.color, 0, drops::appIcon.dy, drops::appIcon.blur, radius);
}

NodePtr progressBar(double fraction) {
    double value = std::clamp(fraction, 0.0, 1.0);
    return canvas([value](Canvas& c, const Frame& f) {
        double radius = f.height / 2;
        c.fillGradient(f, sheenGradient(sheen::track), radius);
        c.innerShadow(f, radius, drops::field.dy, drops::field.blur, Color::black(tokens::opacity::blackShade));
        if (value > 0) c.fillGradient({f.x, f.y, f.width * value, f.height}, glossGradient(gloss::progress), radius);
        c.stroke(f, Color::black(tokens::opacity::blackShade), borders::pixels, radius);
    })->frameSize(std::nullopt, metrics::progress);
}

NodePtr blackBar() {
    return canvas([](Canvas& c, const Frame& f) {
        c.fill(f, Color::hex(tokens::gloss::black::bottom, shadows::blackBar));
        c.fillGradient({f.x, f.y, f.width, f.height / 2}, Gradient(Color::white(tokens::opacity::blackTop), Color::white(tokens::opacity::blackMid)));
        c.fill({f.x, f.y, f.width, borders::pixels}, Color::white(tokens::opacity::blackEdge));
        c.fill({f.x, f.maxY() - borders::pixels, f.width, borders::pixels}, Color::black(tokens::opacity::blackShade));
    });
}

NodePtr stripes() {
    return canvas([](Canvas& c, const Frame& f) { c.stripes(f, pattern::base, pattern::stripe, pattern::width, pattern::period); });
}

NodePtr navigationBar() {
    return canvas([](Canvas& c, const Frame& f) {
        c.boxShadow(f, 0, 0, drops::bar.dy, drops::bar.blur, drops::bar.color);
        c.fillGradient(f, glossGradient(gloss::bar));
        c.fill({f.x, f.y, f.width, borders::pixels}, Color::white(tokens::opacity::barLight));
        c.fill({f.x, f.maxY() - borders::pixels, f.width, borders::pixels}, gloss::bar.border);
    });
}

NodePtr recess() {
    return canvas([](Canvas& c, const Frame& f) {
        c.fillGradient(f, Gradient(Color::hex(tokens::surface::navigationActive::top, tokens::opacity::navigationTop), Color::hex(tokens::surface::navigationActive::bottom, tokens::opacity::navigationBottom)), metrics::switchRadius);
        c.innerShadow(f, metrics::switchRadius, drops::pressed.dy, drops::pressed.blur, drops::pressed.color);
    });
}

NodePtr sheet(const Sheen& colors_, bool bottomEdge) {
    Sheen s = colors_;
    return canvas([s, bottomEdge](Canvas& c, const Frame& f) {
        c.fillGradient(f, sheenGradient(s));
        if (bottomEdge) c.fill({f.x, f.maxY() - borders::pixels, f.width, borders::pixels}, s.border);
        c.fill({f.x, f.y, f.width, borders::pixels}, Color::white(tokens::opacity::barLight));
    });
}

NodePtr frameImage(NodePtr content, double radius) { return content->clipped(radius)->overlay(strokeRectangle(Color::hex(tokens::gloss::white::border), borders::pixels, radius))->boxShadow(Color::black(drops::button.color.a), 0, drops::button.dy, drops::button.blur, radius); }

NodePtr reflected(Image picture, double width, double height) {
    return canvas([picture](Canvas& c, const Frame& f) {
        Frame source{f.x, f.y - f.height, f.width, f.height};
        c.reflection(picture, source, f.height, tokens::opacity::reflection);
    })->frameSize(width, height);
}

NodePtr glyph(const std::string& name, double size, std::optional<Color> color) {
    const glyphs::Glyph* found = findGlyph(name);
    return reader([found, size, color](const Environment& env) {
        Color tint = color.value_or(env.foreground.value_or(colors::textSecondary));
        if (!found) return rectangle(Color::clear())->frameSize(size, size);
        return canvas([found, tint](Canvas& c, const Frame& f) { c.path(found->steps, found->count, f, tokens::icon::large, tint); }, Size{size, size})->frameSize(size, size);
    });
}

NodePtr icon(const std::string& name, double size, const std::string& fallback) {
    double side = std::min(size, iconSize::ribbon);
    if (icons::packed(name) && icons::load(name).valid())
        return canvas([name, side](Canvas& c, const Frame& f) {
            int pixels = static_cast<int>(std::lround(side * c.scale()));
            Image picture = icons::sized(name, pixels);
            double drawn = static_cast<double>(picture.pixelsWide) / c.scale();
            c.pixelImage(picture, {f.x + (f.width - drawn) / 2, f.y + (f.height - drawn) / 2, drawn, drawn});
        }, Size{side, side})->frameSize(side, side);
    std::string substitute = icons::placeholder(name);
    if (!substitute.empty()) return glyph(substitute, side, colors::textSecondary);
    if (fallback.empty()) return rectangle(Color::clear())->frameSize(side, side);
    return text(fallback, Font{std::string(tokens::font::family), std::max(tokens::studioFont::micro, size * 0.62), Weight::bold})->fixed();
}

NodePtr symbol(const std::string& value, double size) { return text(value, Font{std::string(tokens::font::family), size, Weight::bold}); }

NodePtr checkbox(std::string id, NodePtr label, bool on, std::function<void(bool)> change) {
    return reader([id, label, on, change](const Environment& env) {
        double side = metrics::checkbox;
        std::vector<NodePtr> layers{rectangle(env.disabled ? colors::surfaceMuted : colors::surface)->frameSize(side, side)->clipped(metrics::smallRadius)->overlay(strokeRectangle(colors::borderStrong, borders::pixels, metrics::smallRadius))};
        if (on) layers.push_back(glyph("spunta", side, env.disabled ? colors::textDisabled : colors::accent));
        auto box = zstack(std::move(layers))->frameSize(side, side);
        auto row = label ? hstack({box, label->font(controlFont(env))->foreground(env.disabled ? colors::textDisabled : colors::textPrimary)}, spacing::s4) : box;
        auto result = plainButton(id, row, [change, on] { change(!on); });
        result->label = on ? ld::tr("Attivo") : ld::tr("Disattivato");
        result->selected = on;
        return result;
    });
}

NodePtr checkbox(std::string id, std::string title, bool on, std::function<void(bool)> change) { return checkbox(std::move(id), text(std::move(title)), on, std::move(change)); }

void drawMark(Canvas& canvas, const Frame& rect, Color color) {
    static const std::vector<Point> outline = {{0, 0}, {192, 98}, {192, 428}, {798, 428}, {798, 98}, {990, 0}, {990, 588}, {0, 588}};
    static const std::vector<Point> diamond = {{325, 161}, {495, 76}, {665, 161}, {495, 246}};
    double scale = std::min(rect.width / 990, rect.height / 588);
    double ox = rect.x + (rect.width - 990 * scale) / 2, oy = rect.y + (rect.height - 588 * scale) / 2;
    auto map = [&](const std::vector<Point>& points) {
        std::vector<Point> out;
        for (auto p : points) out.push_back({ox + p.x * scale, oy + p.y * scale});
        return out;
    };
    canvas.polygons({map(outline), map(diamond)}, color, true);
}

NodePtr logo(double height, std::string label) {
    return reader([height, label](const Environment& env) {
        Color color = env.foreground.value_or(colors::textPrimary);
        auto mark = canvas([color](Canvas& c, const Frame& f) { drawMark(c, f, color); })->frameSize(height * 990.0 / 588.0, height)->fixed();
        mark->label = label;
        mark->accessibility = "logo";
        return mark;
    });
}

NodePtr loading(std::string message) {
    auto node = text(message.empty() ? ld::tr("Carico…") : message, type::small, colors::textSecondary);
    node->label = "Caricamento";
    return node;
}

Color genreColor(const std::string& genre) {
    std::size_t index = 0;
    for (std::size_t i = 0; i < ld::rules::genres.size(); ++i)
        if (ld::rules::genres[i] == genre) index = i + 1;
    static const std::array<std::size_t, 8> picks{3, 8, 11, 0, 21, 22, 4, 9};
    return Color::hex(std::string(ld::rules::part::palette[picks[index % picks.size()]]));
}

NodePtr genreTile(const std::string& genre) {
    Color tint = genreColor(genre);
    return canvas([tint](Canvas& c, const Frame& f) {
        c.fill(f, tint);
        double side = std::min(f.width, f.height) * 0.42;
        drawMark(c, {f.midX() - side / 2, f.midY() - side / 2, side, side}, colors::headerText);
    });
}

}
