#pragma once

#include "Interface/Surface.hpp"

#include <string>
#include <vector>

namespace audit {

inline std::vector<std::string> cursorProblems(const ui::Surface& surface) {
    std::vector<std::string> found;
    for (const auto& region : surface.regionInfo()) {
        std::string name = region.id.empty() ? "senza id" : region.id;
        if (region.cursor == ui::Cursor::pointer && !region.actionable) found.push_back(name + ": mano senza azione");
        if (region.hovers && !region.actionable && !region.watches) found.push_back(name + ": hover senza azione");
        if (!region.enabled && surface.cursorAt({region.frame.midX(), region.frame.midY()}) != ui::Cursor::arrow) found.push_back(name + ": mano su un elemento disattivato");
    }
    return found;
}

}
