#include "GuiOverlay.hpp"

#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/Math.hpp"
#include "Interface/Surface.hpp"
#include "Interface/Tokens.hpp"

namespace ui {

namespace {

Color hex(const std::string& value, double alpha = 1) {
    ld::Color c = ld::Color::hex(value);
    return {c.r, c.g, c.b, alpha};
}

class Board final : public Node {
public:
    std::vector<Frame> frames;
    std::vector<Frame> clips;
    Size measure(Proposal p) override { return {p.width.value_or(0), p.height.value_or(0)}; }
    bool expandsIn(Axis) const override { return true; }
    void place(Frame bounds) override {
        frame = bounds;
        for (std::size_t i = 0; i < children.size(); ++i) children[i]->place({bounds.x + frames[i].x, bounds.y + frames[i].y, std::max(0.0, frames[i].width), std::max(0.0, frames[i].height)});
    }
    void draw(Canvas& c, Surface& s) override {
        for (std::size_t i = 0; i < children.size(); ++i) {
            Frame clip{frame.x + clips[i].x, frame.y + clips[i].y, std::max(0.0, clips[i].width), std::max(0.0, clips[i].height)};
            c.save();
            c.clip(clip);
            s.pushClip(clip);
            children[i]->draw(c, s);
            s.popClip();
            c.restore();
        }
    }
};

class Picture final : public Node {
public:
    Image source;
    Color tint;
    double alpha = 1;
    Size measure(Proposal p) override { return {p.width.value_or(0), p.height.value_or(0)}; }
    bool expandsIn(Axis) const override { return true; }
    void draw(Canvas& c, Surface& s) override {
        c.imageMultiplied(source, frame, tint, alpha);
        Node::draw(c, s);
    }
};

Alignment alignmentOf(const ld::GuiElement& e) {
    HAlign h = e.horizontal == "Left" ? HAlign::leading : e.horizontal == "Right" ? HAlign::trailing : HAlign::center;
    VAlign v = e.vertical == "Top" ? VAlign::top : e.vertical == "Bottom" ? VAlign::bottom : VAlign::center;
    return {h, v};
}

TextAlign textAlign(const ld::GuiElement& e) { return e.horizontal == "Left" ? TextAlign::leading : e.horizontal == "Right" ? TextAlign::trailing : TextAlign::center; }

}

Font guiFont(const std::string& name, double size) {
    std::string family{ui::tokens::font::family};
    if (name == "SourceSansBold" || name == "ArialBold" || name == "GothamBold" || name == "GothamBlack" || name == "SourceSansSemibold") return {family, size, Weight::bold};
    if (name == "SourceSansLight") return {family, size, Weight::light};
    if (name == "Code") return {"Menlo", size, Weight::regular, true};
    if (name == "Fantasy" || name == "Cartoon") return Font::named("Chalkboard SE", size);
    if (name == "Garamond" || name == "Antique") return Font::named("Georgia", size);
    if (name == "Highway" || name == "SciFi" || name == "Arcade") return Font::named("Futura", size);
    return Font::named(family, size);
}

NodePtr GuiOverlay::element(const ld::GuiElement& e, const GuiActions& actions, bool interactive) {
    Surface* surface = Surface::active();
    bool pressed = surface && surface->pressed("gui." + e.id);
    std::vector<NodePtr> layers{rectangle(hex(e.background, e.backgroundAlpha * (pressed ? 0.8 : 1)))};
    if (e.borderSize > 0 && e.backgroundAlpha > 0) layers.push_back(strokeRectangle(hex(e.border, e.backgroundAlpha), e.borderSize));
    auto label = [&] {
        double size = e.scaled ? std::max(8.0, e.frame.height() * 0.8) : e.textSize;
        NodePtr t = text(e.text, guiFont(e.font, size), hex(e.textColor, e.textAlpha));
        if (!e.wrapped) t = lineLimited(t, 1);
        if (e.scaled) t = scaledText(t, 0.1);
        return t->multilineAlignment(textAlign(e))->fill(alignmentOf(e));
    };
    auto picture = [&]() -> NodePtr {
        auto asset = ld::asset_cache::content(ld::PropertyValue::content(e.image));
        if (!asset) return nullptr;
        auto path = ld::asset_cache::verified(*asset);
        if (!path) return nullptr;
        Image source = loadImage(*path);
        if (!source.valid()) return nullptr;
        auto node = std::make_shared<Picture>();
        node->source = source;
        node->tint = hex(e.imageColor);
        node->alpha = e.imageAlpha;
        return node;
    };
    std::string id = "gui." + e.id;
    switch (e.role) {
    case ld::GuiElement::Role::label: layers.push_back(label()); break;
    case ld::GuiElement::Role::button: {
        auto content = label();
        if (interactive) content = content->onTap(id, [activate = actions.activate, target = e.id] { if (activate) activate(target); });
        layers.push_back(content);
        break;
    }
    case ld::GuiElement::Role::box: {
        if (shown[e.id] != e.text) {
            shown[e.id] = e.text;
            drafts[e.id] = e.text;
        }
        NativeField field;
        field.id = id;
        field.prompt = e.hint;
        field.text = drafts[e.id];
        field.font = guiFont(e.font, e.textSize);
        field.color = hex(e.textColor);
        field.alignment = textAlign(e);
        field.editable = interactive;
        field.changed = [this, target = e.id](const std::string& value) {
            drafts[target] = value;
            if (invalidated) invalidated();
        };
        field.commit = [this, commit = actions.commit, target = e.id](const std::string& value) {
            drafts[target] = value;
            if (commit) commit(target, value);
        };
        layers.push_back(textField(field)->paddingHorizontal(2)->fill());
        break;
    }
    case ld::GuiElement::Role::image:
        if (auto p = picture()) layers.push_back(p);
        break;
    case ld::GuiElement::Role::imageButton: {
        NodePtr content = picture();
        if (!content) content = rectangle(Color::clear());
        if (interactive) content = content->onTap(id, [activate = actions.activate, target = e.id] { if (activate) activate(target); });
        layers.push_back(content);
        break;
    }
    case ld::GuiElement::Role::scroller:
        if (interactive) layers.push_back(rectangle(Color::clear())->onDrag(id, [scroll = actions.scroll, target = e.id](const DragEvent& d) {
            if (!d.ended && scroll) scroll(target, -d.translation.y / 8);
        }));
        break;
    case ld::GuiElement::Role::frame: break;
    }
    return zstack(std::move(layers));
}

NodePtr GuiOverlay::build(const std::vector<ld::GuiNode>& screens, const GuiActions& actions, bool interactive) {
    return geometry([this, screens, actions, interactive](Size size) {
        auto board = std::make_shared<Board>();
        for (const auto& e : ld::gui_layout::resolve(screens, size.width, size.height)) {
            board->children.push_back(element(e, actions, interactive));
            board->frames.push_back({e.frame.minX(), e.frame.minY(), e.frame.width(), e.frame.height()});
            board->clips.push_back(e.clip.null ? Frame{} : Frame{e.clip.minX(), e.clip.minY(), e.clip.width(), e.clip.height()});
        }
        return NodePtr(board);
    });
}

}
