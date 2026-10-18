#include "StudioSync.hpp"

#include "Localization/Localization.hpp"

namespace ld {

StudioSync::StudioSync(std::shared_ptr<Backend> owner, std::shared_ptr<HttpTransport> transport) : backend(owner), assets(owner, std::move(transport)) {}

std::optional<std::string> StudioSync::semanticDifference(const Experience& sent, const Experience& stored) {
    if (sent.id != stored.id) return tr("identificatore");
    if (sent.title != stored.title) return tr("titolo");
    if (sent.genre != stored.genre) return tr("genere");
    if (sent.serverSize != stored.serverSize) return tr("dimensione del server");
    if (!(sent.lighting == stored.lighting)) return tr("illuminazione");
    if (!(sent.terrain == stored.terrain)) return tr("terreno");
    if (sent.blocks.size() != stored.blocks.size()) return tr("numero di oggetti");
    for (std::size_t i = 0; i < sent.blocks.size(); ++i)
        if (!(sent.blocks[i] == stored.blocks[i])) return tr("oggetto {0}", sent.blocks[i].displayName());
    if (sent.instances.size() != stored.instances.size()) return tr("numero di istanze");
    for (std::size_t i = 0; i < sent.instances.size(); ++i)
        if (!(sent.instances[i] == stored.instances[i])) return tr("istanza {0}", sent.instances[i].name);
    return std::nullopt;
}

Async<StudioReadBack> StudioSync::save(Experience outgoing, bool publish) {
    co_await assets.upload(outgoing);
    Json storedJson = co_await backend->cloud(publish ? "LudisseyPublishWorld" : "LudisseySaveWorld", Json::object({{"world", outgoing.json()}}));
    auto stored = Experience::fromJson(storedJson);
    if (!stored) throw ServiceError(stored.error().message);
    Json listed = co_await backend->cloud("LudisseyWorlds", Json::object());
    std::optional<Experience> draft;
    for (const auto& item : listed["worlds"].items()) {
        auto world = Experience::fromJson(item);
        if (!world) throw ServiceError(world.error().message);
        if (world->id == outgoing.id) draft = *world;
    }
    if (!draft) throw SaveVerificationError(tr("Il progetto non risulta salvato sul server."));
    if (auto difference = semanticDifference(outgoing, *draft)) throw SaveVerificationError(tr("Il server ha memorizzato un valore diverso per {0}.", *difference));
    if (draft->revision != stored->revision || draft->updatedAt != stored->updatedAt) throw SaveVerificationError(tr("Non siamo sicuri che il salvataggio sia andato a buon fine. Salva di nuovo."));
    if (!publish) co_return StudioReadBack{*draft, std::nullopt};
    Json response = co_await backend->cloud("LudisseyExperience", Json::object({{"id", outgoing.id}, {"world", true}}));
    auto details = ExperienceDetails::fromJson(response);
    if (!details) throw ServiceError(details.error().message);
    if (!response.contains("world")) throw SaveVerificationError(tr("L’esperienza pubblicata non si può rileggere."));
    auto live = Experience::fromJson(response["world"]);
    if (!live) throw ServiceError(live.error().message);
    if (auto difference = semanticDifference(outgoing, *live)) throw SaveVerificationError(tr("{0} è diverso nella versione pubblica.", *difference));
    const auto& facts = details->experience;
    if (!facts.published || facts.revision != stored->revision || facts.genre != outgoing.genre || facts.serverSize != outgoing.serverSize || facts.owner != stored->owner)
        throw SaveVerificationError(tr("I dati del catalogo non corrispondono al progetto."));
    co_return StudioReadBack{*draft, *details};
}

}
