#include "EngineTypes.hpp"

#include "Lim/Lexer/Lexer.hpp"
#include "Localization/Localization.hpp"

#include <cctype>
#include <memory>

namespace ld {

using lim::HostClass;
using lim::HostEvent;
using lim::HostGlobal;
using lim::HostMember;
using lim::HostMethod;
using lim::Type;
using lim::TypePtr;

lim::ExecutionContext limContext(ScriptContext context) {
    switch (context) {
    case ScriptContext::server: return lim::ExecutionContext::server;
    case ScriptContext::client: return lim::ExecutionContext::client;
    case ScriptContext::edit: return lim::ExecutionContext::edit;
    }
    return lim::ExecutionContext::shared;
}

std::string_view contextTitle(ScriptContext context) {
    switch (context) {
    case ScriptContext::server: return "server";
    case ScriptContext::client: return "client";
    case ScriptContext::edit: return "edit";
    }
    return "server";
}

std::string limMemberName(std::string_view registryName, std::string_view typeName) {
    std::string name(registryName);
    if (name == "CFrame" || name == "CoordinateFrame") return "transform";
    if ((typeName == "Color3") && name.size() > 1 && name.back() == '3') name.pop_back();
    std::size_t run = 0;
    while (run < name.size() && std::isupper(static_cast<unsigned char>(name[run]))) ++run;
    if (run == 0) return name;
    std::size_t lower = run == name.size() || run == 1 || !std::islower(static_cast<unsigned char>(name[run])) ? run : run - 1;
    for (std::size_t i = 0; i < lower; ++i) name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    return name;
}

TypePtr limType(const PropertyDescriptor& property) {
    const std::string& t = property.typeName;
    if (property.category == "Primitive") {
        if (t == "bool") return Type::boolean();
        if (t == "int" || t == "int64") return Type::integer();
        if (t == "float" || t == "double") return Type::number();
        if (t == "string") return Type::text();
        return Type::any();
    }
    if (property.category == "DataType") {
        if (t == "Vector3") return Type::host("Vec3");
        if (t == "Vector2") return Type::host("Vec2");
        if (t == "Color3" || t == "BrickColor") return Type::host("Color");
        if (t == "UDim") return Type::host("Dim");
        if (t == "UDim2") return Type::host("Dim2");
        if (t == "CFrame") return Type::host("Transform");
        if (t == "Ray") return Type::host("Ray");
        if (t == "Content" || t == "ProtectedString") return Type::text();
        if (t == "Object") return Type::optional(Type::host("Instance"));
        return Type::any();
    }
    if (property.category == "Enum") return Type::enumeration(t);
    if (property.category == "Class") return Type::optional(Type::host(t));
    return Type::any();
}

namespace {

struct Parameter {
    std::string name;
    TypePtr type;
};

HostMethod hostMethod(std::string name, std::vector<Parameter> parameters, int minimum, TypePtr result, std::string doc, bool async = false, bool variadic = false,
                  lim::ExecutionContext only = lim::ExecutionContext::shared) {
    HostMethod m;
    m.name = std::move(name);
    for (auto& p : parameters) {
        m.parameterNames.push_back(p.name);
        m.parameters.push_back(p.type);
    }
    m.minimum = minimum;
    m.result = result ? result : Type::none();
    m.doc = std::move(doc);
    m.async = async;
    m.variadic = variadic;
    m.only = only;
    return m;
}

struct Signature {
    std::string registry;
    HostMethod definition;
};

}

const std::set<std::string>& EngineTypes::emittedEvents() {
    static const std::set<std::string> names = {"Changed", "ChildAdded", "ChildRemoved", "DescendantAdded", "DescendantRemoving", "Touched", "TouchEnded", "PlayerAdded", "PlayerRemoving",
                                                "CharacterAdded", "CharacterRemoving", "Died", "HealthChanged", "Heartbeat", "Stepped", "RenderStepped", "OnServerEvent", "OnClientEvent",
                                                "Event", "Completed", "Activated", "Equipped", "Unequipped", "FocusLost", "InputBegan", "InputEnded", "MouseButton1Click", "MouseButton1Down",
                                                "MouseButton1Up", "SelectionChanged"};
    return names;
}

namespace {

std::vector<Signature> signatures() {
    auto T = Type::text(), B = Type::boolean(), N = Type::number(), A = Type::any();
    auto instance = Type::host("Instance");
    auto maybe = Type::optional(instance);
    auto instances = Type::list(instance);
    auto server = lim::ExecutionContext::server, client = lim::ExecutionContext::client, edit = lim::ExecutionContext::edit;
    auto dim2 = Type::host("Dim2");
    auto direction = Type::enumeration("EasingDirection"), style = Type::enumeration("EasingStyle");
    return {
        {"FindFirstChild", hostMethod("findFirstChild", {{"name", T}, {"recursive", B}}, 1, maybe, tr("Primo figlio con questo nome, oppure none."))},
        {"FindFirstChildOfClass", hostMethod("findFirstChildOfClass", {{"className", T}}, 1, maybe, tr("Primo figlio della classe indicata."))},
        {"FindFirstChildWhichIsA", hostMethod("findFirstChildWhichIsA", {{"className", T}}, 1, maybe, tr("Primo figlio che è della classe o di una sottoclasse."))},
        {"FindFirstAncestor", hostMethod("findFirstAncestor", {{"name", T}}, 1, maybe, tr("Primo antenato con questo nome."))},
        {"FindFirstAncestorOfClass", hostMethod("findFirstAncestorOfClass", {{"className", T}}, 1, maybe, tr("Primo antenato della classe indicata."))},
        {"FindFirstAncestorWhichIsA", hostMethod("findFirstAncestorWhichIsA", {{"className", T}}, 1, maybe, tr("Primo antenato che è della classe o di una sottoclasse."))},
        {"GetChildren", hostMethod("getChildren", {}, 0, instances, "Figli diretti.")},
        {"GetDescendants", hostMethod("getDescendants", {}, 0, instances, "Tutti i discendenti.")},
        {"IsA", hostMethod("isA", {{"className", T}}, 1, B, tr("Vero se l’istanza è della classe o di una sottoclasse."))},
        {"Clone", hostMethod("clone", {}, 0, maybe, tr("Copia l’istanza con i suoi figli."))},
        {"Destroy", hostMethod("destroy", {}, 0, nullptr, "Rimuove l'istanza e i suoi figli.")},
        {"ClearAllChildren", hostMethod("clearAllChildren", {}, 0, nullptr, "Rimuove tutti i figli.")},
        {"GetFullName", hostMethod("getFullName", {}, 0, T, tr("Percorso completo, per esempio Workspace.Casa.Porta."))},
        {"IsAncestorOf", hostMethod("isAncestorOf", {{"descendant", instance}}, 1, B, tr("Vero se l’altra istanza discende da questa."))},
        {"IsDescendantOf", hostMethod("isDescendantOf", {{"ancestor", instance}}, 1, B, tr("Vero se questa istanza discende dall’altra."))},
        {"WaitForChild", hostMethod("waitForChild", {{"name", T}, {"timeout", Type::optional(N)}}, 1, maybe, tr("Attende un figlio con questo nome."), true)},
        {"GetPropertyChangedSignal", hostMethod("getPropertyChangedSignal", {{"property", T}}, 1, Type::host("Event"), tr("Evento emesso quando la proprietà cambia."))},
        {"GetService", hostMethod("getService", {{"name", T}}, 1, instance, tr("Servizio con questo nome."))},
        {"FindService", hostMethod("findService", {{"name", T}}, 1, maybe, tr("Servizio con questo nome, oppure none."))},
        {"GetPlayers", hostMethod("getPlayers", {}, 0, Type::list(Type::host("Player")), tr("Giocatori collegati."))},
        {"GetPlayerFromCharacter", hostMethod("getPlayerFromCharacter", {{"character", maybe}}, 1, Type::optional(Type::host("Player")), tr("Giocatore che controlla il personaggio."))},
        {"MoveTo", hostMethod("moveTo", {{"position", Type::host("Vec3")}}, 1, nullptr, tr("Sposta il modello mantenendo la forma."))},
        {"SetPrimaryPartCFrame", hostMethod("setPrimaryPartTransform", {{"transform", Type::host("Transform")}}, 1, nullptr, tr("Sposta il modello dalla parte principale."))},
        {"GetPrimaryPartCFrame", hostMethod("getPrimaryPartTransform", {}, 0, Type::optional(Type::host("Transform")), tr("Posizione della parte principale."))},
        {"TakeDamage", hostMethod("takeDamage", {{"amount", N}}, 1, nullptr, tr("Toglie salute al personaggio."))},
        {"Play", hostMethod("play", {}, 0, nullptr, tr("Avvia la riproduzione."))},
        {"Resume", hostMethod("resume", {}, 0, nullptr, tr("Riprende la riproduzione."))},
        {"Stop", hostMethod("stop", {}, 0, nullptr, tr("Ferma la riproduzione."))},
        {"Pause", hostMethod("pause", {}, 0, nullptr, "Mette in pausa.")},
        {"Cancel", hostMethod("cancel", {}, 0, nullptr, "Annulla l'animazione.")},
        {"Fire", hostMethod("fire", {{"values", A}}, 0, nullptr, tr("Emette l’evento con i valori indicati."), false, true)},
        {"Invoke", hostMethod("invoke", {{"values", A}}, 0, A, tr("Chiama la funzione collegata e restituisce il risultato."), false, true)},
        {"FireServer", hostMethod("fireServer", {{"values", A}}, 0, nullptr, tr("Invia i valori al server."), false, true, client)},
        {"FireClient", hostMethod("fireClient", {{"player", Type::host("Player")}, {"values", A}}, 1, nullptr, tr("Invia i valori a un giocatore."), false, true, server)},
        {"FireAllClients", hostMethod("fireAllClients", {{"values", A}}, 0, nullptr, tr("Invia i valori a tutti i giocatori."), false, true, server)},
        {"InvokeServer", hostMethod("invokeServer", {{"values", A}}, 0, A, tr("Chiama il server e restituisce il risultato."), false, true, client)},
        {"InvokeClient", hostMethod("invokeClient", {{"player", Type::host("Player")}, {"values", A}}, 1, A, tr("Chiama un giocatore e restituisce il risultato."), false, true, server)},
        {"AddItem", hostMethod("addItem", {{"item", instance}, {"lifetime", N}}, 1, nullptr, tr("Distrugge l’istanza dopo i secondi indicati."))},
        {"Get", hostMethod("get", {}, 0, instances, "Istanze selezionate in Studio.", false, false, edit)},
        {"Set", hostMethod("set", {{"selection", instances}}, 1, nullptr, tr("Seleziona le istanze in Studio."), false, false, edit)},
        {"IsClient", hostMethod("isClient", {}, 0, B, tr("Vero sul client."))},
        {"IsServer", hostMethod("isServer", {}, 0, B, tr("Vero sul server."))},
        {"IsStudio", hostMethod("isStudio", {}, 0, B, tr("Vero dentro Ludissey Studio."))},
        {"IsRunning", hostMethod("isRunning", {}, 0, B, tr("Vero mentre l’esperienza è in esecuzione."))},
        {"IsKeyDown", hostMethod("isKeyDown", {{"key", Type::enumeration("KeyCode")}}, 1, B, tr("Vero se il tasto è premuto."), false, false, client)},
        {"GetFocusedTextBox", hostMethod("getFocusedTextBox", {}, 0, Type::optional(Type::host("TextBox")), tr("Casella di testo attiva."), false, false, client)},
        {"CaptureFocus", hostMethod("captureFocus", {}, 0, nullptr, tr("Attiva la casella di testo."), false, false, client)},
        {"ReleaseFocus", hostMethod("releaseFocus", {}, 0, nullptr, tr("Disattiva la casella di testo."), false, false, client)},
        {"IsFocused", hostMethod("isFocused", {}, 0, B, tr("Vero se la casella è attiva."), false, false, client)},
        {"Create", hostMethod("create", {{"instance", instance}, {"goals", Type::map(T, A)}, {"duration", N}, {"style", style}, {"direction", direction}}, 2, Type::host("Tween"), tr("Prepara un’animazione delle proprietà."))},
        {"TweenPosition", hostMethod("tweenPosition", {{"position", dim2}, {"direction", direction}, {"style", style}, {"duration", N}}, 1, B, tr("Anima la posizione dell’elemento."))},
        {"TweenSize", hostMethod("tweenSize", {{"size", dim2}, {"direction", direction}, {"style", style}, {"duration", N}}, 1, B, tr("Anima la dimensione dell’elemento."))},
        {"TweenSizeAndPosition", hostMethod("tweenSizeAndPosition", {{"size", dim2}, {"position", dim2}, {"direction", direction}, {"style", style}, {"duration", N}}, 2, B, "Anima dimensione e posizione.")},
        {"JSONEncode", hostMethod("jsonEncode", {{"value", A}}, 1, T, tr("Converte un valore in JSON."))},
        {"JSONDecode", hostMethod("jsonDecode", {{"text", T}}, 1, A, tr("Legge un testo JSON."))},
        {"GenerateGUID", hostMethod("generateGuid", {{"braces", B}}, 0, T, "Identificatore univoco casuale.")},
    };
}

TypePtr eventParameter(const std::string& name) {
    if (name == "player") return Type::host("Player");
    if (name == "otherPart" || name == "part" || name == "hit") return Type::host("BasePart");
    if (name == "child" || name == "descendant" || name == "character" || name == "instance" || name == "parent") return Type::host("Instance");
    if (name == "input") return Type::host("InputObject");
    if (name == "gameProcessedEvent" || name == "enterPressed") return Type::boolean();
    if (name == "property") return Type::text();
    if (name == "health" || name == "step" || name == "time" || name == "deltaTime") return Type::number();
    return Type::any();
}

}

const EngineTypes& EngineTypes::shared() {
    static std::unique_ptr<EngineTypes> instance;
    static const ClassRegistry* owner = nullptr;
    if (!instance || owner != &ClassRegistry::shared()) {
        owner = &ClassRegistry::shared();
        instance = std::make_unique<EngineTypes>(*owner);
    }
    return *instance;
}

EngineTypes::EngineTypes(const ClassRegistry& registry) : classes(registry) {
    lim::addStandardLibrary(types);
    for (const auto& [name, items] : registry.enums()) types.addEnum(name, items);
    types.addEnum("TerrainMaterial", {"air", "grass", "rock", "sand", "mud", "water"});
    for (const auto& [name, descriptor] : registry.descriptors()) addClass(descriptor);
    addEngineLibrary();
}

void EngineTypes::addClass(const ClassDescriptor& descriptor) {
    static const std::vector<Signature> table = signatures();
    HostClass c;
    c.name = descriptor.className;
    c.base = descriptor.superclass.value_or("");
    c.doc = descriptor.category.empty() ? descriptor.className : descriptor.category;
    MemberNames& own = names[descriptor.className];
    auto preferred = [](const std::string& existing, const std::string& candidate) { return std::isupper(static_cast<unsigned char>(candidate[0])) && !std::isupper(static_cast<unsigned char>(existing[0])); };
    for (const auto& [name, property] : descriptor.properties) {
        if (property.deprecated) continue;
        std::string lim = limMemberName(name, property.typeName);
        if (lim::isKeyword(lim)) continue;
        if (auto known = own.properties.find(lim); known != own.properties.end() && !preferred(known->second, name)) continue;
        own.properties[lim] = name;
        own.propertyLim[name] = lim;
        HostMember m;
        m.name = lim;
        m.type = limType(property);
        m.readOnly = property.readOnly || (property.typeName == "Object" && name != "Parent");
        m.doc = property.category + " " + property.typeName;
        c.properties[lim] = m;
    }
    for (const auto& signature : table) {
        if (!descriptor.functions.count(signature.registry)) continue;
        own.methods[signature.definition.name] = signature.registry;
        c.methods[signature.definition.name] = signature.definition;
    }
    if (descriptor.functions.count("FindFirstChild")) {
        auto find = c.methods.at("findFirstChild");
        find.name = "find";
        c.methods["find"] = std::move(find);
        own.methods["find"] = "FindFirstChild";
        c.methods["add"] = hostMethod("add", {{"child", Type::host("Instance")}}, 1, Type::none(), tr("Aggiunge un figlio rispettando le regole della gerarchia."));
        own.methods["add"] = "$add";
    }
    for (const auto& event : descriptor.events) {
        if (!emittedEvents().count(event)) continue;
        std::string lim = limMemberName(event);
        if (lim::isKeyword(lim)) continue;
        if (auto known = own.events.find(lim); known != own.events.end() && !preferred(known->second, event)) continue;
        own.events[lim] = event;
        HostEvent e;
        e.name = lim;
        if (auto parameters = descriptor.eventParameters.find(event); parameters != descriptor.eventParameters.end())
            for (const auto& parameter : parameters->second) {
                e.parameterNames.push_back(parameter);
                e.parameters.push_back(eventParameter(parameter));
            }
        c.events[lim] = e;
    }
    for (const auto& [original, alias] : std::vector<std::pair<std::string, std::string>>{{"PlayerAdded", "joined"}, {"PlayerRemoving", "left"}, {"MouseButton1Click", "clicked"}}) {
        if (!descriptor.events.count(original)) continue;
        auto event = c.events.at(limMemberName(original));
        event.name = alias;
        c.events[alias] = std::move(event);
        own.events[alias] = original;
    }
    addCreatorMembers(descriptor, c, own);
    if (descriptor.canCreate()) {
        c.creatable = true;
        c.constructor = hostMethod(descriptor.className, {{"parent", Type::optional(Type::host("Instance"))}, {"name", Type::text()}}, 0, Type::host(descriptor.className),
                               tr("Crea un nuovo {0}.", descriptor.className));
    }
    types.addClass(std::move(c));
}

void EngineTypes::addCreatorMembers(const ClassDescriptor& descriptor, HostClass& c, MemberNames& own) {
    auto member = [](std::string name, TypePtr type, bool readOnly, std::string doc) {
        HostMember m;
        m.name = std::move(name);
        m.type = std::move(type);
        m.readOnly = readOnly;
        m.doc = std::move(doc);
        return m;
    };
    const std::string& name = descriptor.className;
    auto character = Type::optional(Type::host("Character"));
    if (descriptor.properties.count("UserId")) {
        c.properties["id"] = member("id", Type::integer(), true, tr("Identificativo stabile del giocatore."));
        own.properties["id"] = "UserId";
    }
    if (name == "Player" && c.properties.count("character")) c.properties["character"].type = character;
    if (name == "Players") {
        c.properties["all"] = member("all", Type::list(Type::host("Player")), true, tr("Giocatori collegati."));
        own.properties["all"] = "$all";
    }
    if (classes.isA(name, "BasePart")) {
        c.properties["character"] = member("character", character, true, tr("Personaggio a cui appartiene la parte, oppure none."));
        own.properties["character"] = "$character";
    }
    if (classes.isA(name, "BasePlayerGui")) {
        c.properties["player"] = member("player", Type::host("ScreenGui"), true, tr("Schermo principale del giocatore: aggiungi qui gli elementi dell’interfaccia."));
        own.properties["player"] = "$screen";
    }
    if (name == "Model") {
        for (const char* p : {"health", "maxHealth", "walkSpeed", "jumpPower", "humanoid", "player"}) own.properties[p] = std::string("$") + p;
        own.methods["takeDamage"] = "$takeDamage";
    }
    if (name == "ReplicatedStorage") {
        c.methods["player"] = hostMethod("player", {{"player", Type::any()}}, 1, Type::host("PlayerData"),
                                         tr("Dati salvati del giocatore locale in questa esperienza (Player oppure il suo id)."), false, false, lim::ExecutionContext::server);
        own.methods["player"] = "$playerData";
    }
    if (name == "RemoteEvent") {
        auto A = Type::any();
        c.methods["send"] = hostMethod("send", {{"values", A}}, 0, nullptr, tr("Invia i valori al server."), false, true, lim::ExecutionContext::client);
        c.methods["sendTo"] = hostMethod("sendTo", {{"player", Type::host("Player")}, {"values", A}}, 1, nullptr, tr("Invia i valori a un giocatore."), false, true, lim::ExecutionContext::server);
        c.methods["broadcast"] = hostMethod("broadcast", {{"values", A}}, 0, nullptr, tr("Invia i valori a tutti i giocatori."), false, true, lim::ExecutionContext::server);
        own.methods["send"] = "FireServer";
        own.methods["sendTo"] = "FireClient";
        own.methods["broadcast"] = "FireAllClients";
        HostEvent received;
        received.name = "received";
        received.variadic = true;
        received.doc = tr("Sul server riceve (giocatore, valori…); sul giocatore riceve (valori…).");
        c.events["received"] = received;
        own.events["received"] = "$received";
    }
    auto retire = [&](const std::string& member, const std::string& replacement) {
        if (auto m = c.methods.find(member); m != c.methods.end()) {
            m->second.deprecated = true;
            m->second.replacement = replacement;
        }
        if (auto e = c.events.find(member); e != c.events.end()) {
            e->second.deprecated = true;
            e->second.replacement = replacement;
        }
    };
    if (name == "RemoteEvent") {
        retire("fireServer", "send");
        retire("fireClient", "sendTo");
        retire("fireAllClients", "broadcast");
        retire("onServerEvent", "received");
        retire("onClientEvent", "received");
    }
    if (name == "Players") {
        retire("playerAdded", "joined");
        retire("playerRemoving", "left");
    }
    if (c.events.count("clicked")) retire("mouseButton1Click", "clicked");
}

void EngineTypes::addEngineLibrary() {
    auto T = Type::text(), B = Type::boolean(), N = Type::number(), A = Type::any();
    auto vec3 = Type::host("Vec3");
    auto instances = Type::list(Type::host("Instance"));
    auto readOnly = [](std::string name, TypePtr type, std::string doc) {
        HostMember m;
        m.name = std::move(name);
        m.type = std::move(type);
        m.readOnly = true;
        m.doc = std::move(doc);
        return m;
    };
    auto handler = Type::function({}, A);
    HostClass event;
    event.name = "Event";
    event.doc = tr("Evento del motore: usa «on oggetto.evento(…) { }» oppure «await oggetto.evento».");
    event.methods["connect"] = hostMethod("connect", {{"handler", handler}}, 1, Type::host("Connection"), tr("Collega una funzione all’evento."));
    event.methods["once"] = hostMethod("once", {{"handler", handler}}, 1, Type::host("Connection"), tr("Collega una funzione per una sola volta."));
    event.methods["wait"] = hostMethod("wait", {}, 0, A, tr("Attende la prossima emissione."), true);
    types.addClass(event);
    HostClass connection;
    connection.name = "Connection";
    connection.doc = tr("Collegamento tra un evento e una funzione.");
    connection.properties["connected"] = readOnly("connected", B, tr("Vero finché il collegamento è attivo."));
    connection.methods["disconnect"] = hostMethod("disconnect", {}, 0, nullptr, tr("Scollega la funzione dall’evento."));
    types.addClass(connection);
    if (const lim::HostClass* run = types.findClass("RunService")) {
        HostClass extended = *run;
        extended.methods["now"] = hostMethod("now", {}, 0, N, tr("Secondi dall’epoca Unix."));
        extended.methods["clock"] = hostMethod("clock", {}, 0, N, tr("Secondi dall’avvio dell’esperienza."));
        names["RunService"].methods["now"] = "$now";
        names["RunService"].methods["clock"] = "$clock";
        types.addClass(extended);
    }
    HostClass camera;
    camera.name = "CameraView";
    camera.doc = tr("Telecamera del giocatore.");
    camera.properties["position"] = readOnly("position", vec3, tr("Posizione della telecamera."));
    camera.properties["focus"] = readOnly("focus", vec3, "Punto osservato.");
    camera.properties["lookVector"] = readOnly("lookVector", vec3, tr("Direzione di vista."));
    camera.properties["fieldOfView"] = readOnly("fieldOfView", N, tr("Campo visivo verticale in gradi."));
    camera.properties["firstPerson"] = readOnly("firstPerson", B, tr("Vero in prima persona."));
    types.addClass(camera);
    HostClass character;
    character.name = "Character";
    character.base = "Model";
    character.doc = tr("Modello con Humanoid controllato da un giocatore o da uno script.");
    auto writable = [](std::string name, TypePtr type, std::string doc) {
        HostMember m;
        m.name = std::move(name);
        m.type = std::move(type);
        m.doc = std::move(doc);
        return m;
    };
    character.properties["health"] = writable("health", N, "Salute attuale.");
    character.properties["maxHealth"] = writable("maxHealth", N, "Salute massima.");
    character.properties["walkSpeed"] = writable("walkSpeed", N, tr("Velocità di camminata."));
    character.properties["jumpPower"] = writable("jumpPower", N, tr("Forza del salto."));
    character.properties["humanoid"] = readOnly("humanoid", Type::host("Humanoid"), tr("Humanoid del personaggio."));
    character.properties["player"] = readOnly("player", Type::optional(Type::host("Player")), tr("Giocatore che controlla il personaggio, oppure none."));
    character.methods["takeDamage"] = hostMethod("takeDamage", {{"amount", N}}, 1, nullptr, tr("Toglie salute al personaggio."));
    types.addClass(character);
    HostClass data;
    data.name = "PlayerData";
    data.doc = tr("Dati persistenti del giocatore per questa esperienza.");
    data.methods["load"] = hostMethod("load", {}, 0, A, tr("Legge i dati salvati, oppure none se non ci sono o il servizio non risponde."), true);
    data.methods["save"] = hostMethod("save", {{"value", A}}, 1, B, tr("Salva testi, numeri, booleani, liste e mappe. Restituisce false se il salvataggio non riesce."), true);
    types.addClass(data);
    HostClass terrain;
    terrain.name = "TerrainView";
    terrain.doc = tr("Terreno dell’esperienza: interrogazioni e modifiche autorevoli.");
    terrain.methods["fillSphere"] = hostMethod("fillSphere", {{"center", vec3}, {"radius", N}, {"material", Type::enumeration("TerrainMaterial")}}, 3, Type::integer(), tr("Riempie una sfera nel terreno e restituisce il numero di celle modificate."), false, false, lim::ExecutionContext::server);
    terrain.methods["height"] = hostMethod("height", {{"x", N}, {"z", N}}, 2, Type::optional(N), tr("Altezza della superficie sotto il punto, oppure none."));
    terrain.methods["isSolid"] = hostMethod("isSolid", {{"point", vec3}}, 1, B, tr("Vero se il punto è dentro il terreno."));
    terrain.methods["isWater"] = hostMethod("isWater", {{"point", vec3}}, 1, B, tr("Vero se il punto è nell’acqua."));
    terrain.methods["raycast"] = hostMethod("raycast", {{"origin", vec3}, {"direction", vec3}}, 2, Type::optional(N), tr("Distanza dal terreno lungo il raggio, oppure none."));
    types.addClass(terrain);
    HostClass hit;
    hit.name = "RaycastHit";
    hit.value = true;
    hit.doc = tr("Risultato di Physics.raycast.");
    hit.properties["instance"] = readOnly("instance", Type::host("BasePart"), tr("Parte colpita."));
    hit.properties["position"] = readOnly("position", vec3, "Punto colpito.");
    hit.properties["distance"] = readOnly("distance", N, "Distanza dall'origine.");
    types.addClass(hit);
    HostClass physics;
    physics.name = "PhysicsView";
    physics.doc = tr("Fisica del mondo.");
    physics.properties["gravity"] = readOnly("gravity", N, tr("Accelerazione di gravità."));
    physics.methods["raycast"] = hostMethod("raycast", {{"origin", vec3}, {"direction", vec3}, {"ignore", instances}}, 2, Type::optional(Type::host("RaycastHit")),
                                        tr("Prima parte colpita lungo il raggio."));
    types.addClass(physics);
    HostClass network;
    network.name = "NetworkView";
    network.doc = tr("Comunicazione tra server e giocatori.");
    network.properties["isServer"] = readOnly("isServer", B, tr("Vero negli script del server."));
    network.properties["isClient"] = readOnly("isClient", B, tr("Vero negli script del giocatore."));
    network.methods["event"] = hostMethod("event", {{"name", T}}, 1, Type::host("RemoteEvent"), tr("Canale con questo nome: usa il RemoteEvent in Storage oppure lo crea."));
    network.methods["remoteFunction"] = hostMethod("remoteFunction", {{"name", T}}, 1, Type::optional(Type::host("RemoteFunction")), tr("RemoteFunction con questo nome in Storage."));
    types.addClass(network);
    HostClass tags;
    tags.name = "TagsView";
    tags.doc = "Etichette sulle istanze.";
    tags.methods["add"] = hostMethod("add", {{"instance", Type::host("Instance")}, {"tag", T}}, 2, nullptr, tr("Aggiunge un’etichetta."));
    tags.methods["remove"] = hostMethod("remove", {{"instance", Type::host("Instance")}, {"tag", T}}, 2, nullptr, tr("Toglie un’etichetta."));
    tags.methods["has"] = hostMethod("has", {{"instance", Type::host("Instance")}, {"tag", T}}, 2, B, tr("Vero se l’istanza ha l’etichetta."));
    tags.methods["tagged"] = hostMethod("tagged", {{"tag", T}}, 1, instances, tr("Istanze con l’etichetta."));
    tags.methods["of"] = hostMethod("of", {{"instance", Type::host("Instance")}}, 1, Type::list(T), "Etichette dell'istanza.");
    types.addClass(tags);
    for (auto context : {ScriptContext::server, ScriptContext::client, ScriptContext::edit})
        for (const auto& [global, className] : serviceGlobals(context)) {
            lim::ExecutionContext only = lim::ExecutionContext::shared;
            if (global == "ServerStorage" || global == "ServerScripts") only = lim::ExecutionContext::server;
            if (global == "Input") only = lim::ExecutionContext::client;
            if (global == "Selection") only = lim::ExecutionContext::edit;
            types.addGlobal({global, Type::host(className), className, only});
        }
    types.addGlobal({"UI", Type::host("BasePlayerGui"), tr("Interfaccia: PlayerGui sul client, StarterGui sul server."), lim::ExecutionContext::shared});
    types.addGlobal({"Camera", Type::host("CameraView"), tr("Telecamera del giocatore."), lim::ExecutionContext::client});
    types.addGlobal({"Terrain", Type::host("TerrainView"), tr("Terreno dell’esperienza."), lim::ExecutionContext::shared});
    types.addGlobal({"Physics", Type::host("PhysicsView"), tr("Raggi e gravità."), lim::ExecutionContext::shared});
    types.addGlobal({"Network", Type::host("NetworkView"), tr("Comunicazione tra server e giocatori."), lim::ExecutionContext::shared});
    types.addGlobal({"Tags", Type::host("TagsView"), "Etichette sulle istanze.", lim::ExecutionContext::shared});
}

const MemberNames* EngineTypes::members(std::string_view className) const {
    auto it = names.find(std::string(className));
    return it == names.end() ? nullptr : &it->second;
}

std::optional<std::string> EngineTypes::property(std::string_view className, std::string_view limName) const {
    const MemberNames* m = members(className);
    if (!m) return std::nullopt;
    auto it = m->properties.find(std::string(limName));
    if (it == m->properties.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> EngineTypes::method(std::string_view className, std::string_view limName) const {
    const MemberNames* m = members(className);
    if (!m) return std::nullopt;
    auto it = m->methods.find(std::string(limName));
    if (it == m->methods.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> EngineTypes::event(std::string_view className, std::string_view limName) const {
    const MemberNames* m = members(className);
    if (!m) return std::nullopt;
    auto it = m->events.find(std::string(limName));
    if (it == m->events.end()) return std::nullopt;
    return it->second;
}

std::string EngineTypes::limProperty(std::string_view className, std::string_view registryName) const {
    const MemberNames* m = members(className);
    if (m)
        if (auto it = m->propertyLim.find(std::string(registryName)); it != m->propertyLim.end()) return it->second;
    return limMemberName(registryName);
}

std::vector<std::pair<std::string, std::string>> EngineTypes::serviceGlobals(ScriptContext context) const {
    std::vector<std::pair<std::string, std::string>> out = {{"World", "Workspace"}, {"Players", "Players"}, {"Lighting", "Lighting"}, {"Storage", "ReplicatedStorage"},
                                                            {"Teams", "Teams"}, {"Audio", "SoundService"}, {"StarterGui", "StarterGui"}, {"StarterPack", "StarterPack"},
                                                            {"StarterPlayer", "StarterPlayer"}, {"Time", "RunService"}, {"HTTP", "HttpService"}, {"Tween", "TweenService"},
                                                            {"Debris", "Debris"}, {"Experience", "DataModel"}};
    if (context != ScriptContext::client) {
        out.push_back({"ServerStorage", "ServerStorage"});
        out.push_back({"ServerScripts", "ServerScriptService"});
    }
    if (context != ScriptContext::server) out.push_back({"Input", "UserInputService"});
    if (context == ScriptContext::edit) out.push_back({"Selection", "Selection"});
    return out;
}

}
