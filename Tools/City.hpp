#pragma once

#include "Scenery.hpp"

#include <memory>

namespace city {

std::unique_ptr<scenery::Scene> build();

}
