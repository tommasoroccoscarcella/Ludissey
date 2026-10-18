#pragma once

#include "Engine/Gui/GuiLayout.hpp"
#include "Interface/Node.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui {

struct GuiActions {
    std::function<void(const std::string&)> activate;
    std::function<void(const std::string&, const std::string&)> commit;
    std::function<void(const std::string&, double)> scroll;
};

class GuiOverlay {
public:
    NodePtr build(const std::vector<ld::GuiNode>& screens, const GuiActions& actions, bool interactive);
    std::function<void()> invalidated;

private:
    std::map<std::string, std::string> drafts;
    std::map<std::string, std::string> shown;
    NodePtr element(const ld::GuiElement& element, const GuiActions& actions, bool interactive);
};

Font guiFont(const std::string& name, double size);

}
