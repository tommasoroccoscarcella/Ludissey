#pragma once

#include "Engine/Backend/Assets.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Experience/Experience.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ld {

class SaveVerificationError : public ServiceError {
public:
    using ServiceError::ServiceError;
};

struct StudioReadBack {
    Experience draft;
    std::optional<ExperienceDetails> details;
};

class StudioSync {
public:
    StudioSync(std::shared_ptr<Backend> backend, std::shared_ptr<HttpTransport> transport = nullptr);
    static std::optional<std::string> semanticDifference(const Experience& sent, const Experience& stored);
    Async<StudioReadBack> save(Experience outgoing, bool publish);

private:
    std::shared_ptr<Backend> backend;
    AssetService assets;
};

}
