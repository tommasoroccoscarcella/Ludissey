#pragma once

#include "Scenery.hpp"

#include <memory>

namespace harbor {

std::unique_ptr<scenery::Scene> build();

}
