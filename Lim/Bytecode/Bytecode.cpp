#include "Bytecode.hpp"

#include "Localization/Localization.hpp"

#include <cstring>
#include <deque>
#include <limits>

namespace lim {

using ld::tr;

namespace {

constexpr std::uint32_t noDefault = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t maximumCount = 1u << 20;

const char* const names[] = {"nop", "constant", "none", "pushTrue", "pushFalse", "pop", "dup", "dup2", "getLocal", "setLocal", "makeCell", "getCell", "setCell",
                             "getUpvalue", "setUpvalue", "getGlobal", "setGlobal", "defineGlobal", "getMember", "getMemberOptional", "setMember", "getIndex",
                             "getIndexOptional", "setIndex", "call", "callNamed", "add", "subtract", "multiply", "divide", "modulo", "equal", "notEqual", "less",
                             "lessEqual", "greater", "greaterEqual", "negate", "logicalNot", "jump", "jumpIfFalse", "jumpIfTrue", "jumpIfFalseKeep", "jumpIfTrueKeep",
                             "jumpIfNotNoneKeep", "jumpIfNoneKeep", "makeList", "makeMap", "makeRange", "iterStart", "iterNext", "interpolate", "closure",
                             "returnValue", "returnNone", "await", "connect", "import", "makeClass", "makeEnum", "isType", "shorthand", "cast"};

static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(Op::count));

class Writer {
public:
    void u8(std::uint8_t v) { out += static_cast<char>(v); }
    void u16(std::uint16_t v) { for (int i = 0; i < 2; ++i) out += static_cast<char>((v >> (8 * i)) & 255); }
    void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 255); }
    void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) out += static_cast<char>((v >> (8 * i)) & 255); }
    void f64(double v) { std::uint64_t bits; std::memcpy(&bits, &v, 8); u64(bits); }
    void text(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())); out += s; }
    std::string out;
};

class Reader {
public:
    explicit Reader(std::string_view data) : data(data) {}
    bool failed = false;
    bool need(std::size_t n) {
        if (failed || position + n > data.size()) { failed = true; return false; }
        return true;
    }
    std::uint8_t u8() { if (!need(1)) return 0; return static_cast<std::uint8_t>(data[position++]); }
    std::uint16_t u16() { std::uint16_t v = 0; if (!need(2)) return 0; for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(data[position++]) << (8 * i)); return v; }
    std::uint32_t u32() { std::uint32_t v = 0; if (!need(4)) return 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[position++])) << (8 * i); return v; }
    std::uint64_t u64() { std::uint64_t v = 0; if (!need(8)) return 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[position++])) << (8 * i); return v; }
    double f64() { std::uint64_t bits = u64(); double v; std::memcpy(&v, &bits, 8); return v; }
    std::string text() {
        std::uint32_t n = u32();
        if (n > (1u << 24) || !need(n)) { failed = true; return {}; }
        std::string s(data.substr(position, n));
        position += n;
        return s;
    }
    std::uint32_t count() {
        std::uint32_t n = u32();
        if (n > maximumCount) failed = true;
        return failed ? 0 : n;
    }
    bool atEnd() const { return position == data.size(); }

private:
    std::string_view data;
    std::size_t position = 0;
};

struct Effect {
    int pops;
    int pushes;
};

Effect effect(const Instruction& i) {
    switch (i.op) {
    case Op::nop: return {0, 0};
    case Op::constant:
    case Op::none:
    case Op::pushTrue:
    case Op::pushFalse: return {0, 1};
    case Op::pop: return {1, 0};
    case Op::dup: return {1, 2};
    case Op::dup2: return {2, 4};
    case Op::getLocal:
    case Op::getCell:
    case Op::getUpvalue:
    case Op::getGlobal: return {0, 1};
    case Op::setLocal:
    case Op::makeCell:
    case Op::setCell:
    case Op::setUpvalue:
    case Op::setGlobal:
    case Op::defineGlobal: return {1, 0};
    case Op::getMember:
    case Op::getMemberOptional: return {1, 1};
    case Op::setMember: return {2, 0};
    case Op::getIndex:
    case Op::getIndexOptional: return {2, 1};
    case Op::setIndex: return {3, 0};
    case Op::call:
    case Op::callNamed: return {static_cast<int>(i.a) + 1, 1};
    case Op::add:
    case Op::subtract:
    case Op::multiply:
    case Op::divide:
    case Op::modulo:
    case Op::equal:
    case Op::notEqual:
    case Op::less:
    case Op::lessEqual:
    case Op::greater:
    case Op::greaterEqual: return {2, 1};
    case Op::negate:
    case Op::logicalNot: return {1, 1};
    case Op::jump: return {0, 0};
    case Op::jumpIfFalse:
    case Op::jumpIfTrue: return {1, 0};
    case Op::jumpIfFalseKeep:
    case Op::jumpIfTrueKeep:
    case Op::jumpIfNotNoneKeep:
    case Op::jumpIfNoneKeep: return {1, 1};
    case Op::makeList: return {static_cast<int>(i.a), 1};
    case Op::makeMap: return {static_cast<int>(i.a) * 2, 1};
    case Op::makeRange: return {2, 1};
    case Op::iterStart: return {1, 1};
    case Op::iterNext: return {1, 1 + static_cast<int>(i.b)};
    case Op::interpolate: return {static_cast<int>(i.a), 1};
    case Op::closure: return {0, 1};
    case Op::returnValue: return {1, 0};
    case Op::returnNone: return {0, 0};
    case Op::await: return {1, 1};
    case Op::connect: return {2, 1};
    case Op::import:
    case Op::makeClass:
    case Op::makeEnum:
    case Op::shorthand: return {0, 1};
    case Op::isType:
    case Op::cast: return {1, 1};
    case Op::count: break;
    }
    return {0, 0};
}

bool isJump(Op op) {
    return op == Op::jump || op == Op::jumpIfFalse || op == Op::jumpIfTrue || op == Op::jumpIfFalseKeep || op == Op::jumpIfTrueKeep || op == Op::jumpIfNotNoneKeep || op == Op::jumpIfNoneKeep || op == Op::iterNext;
}

std::optional<std::string> validatePrototype(const Module& module, std::size_t index) {
    const Prototype& p = module.prototypes[index];
    std::string where = "funzione " + (p.name.empty() ? std::to_string(index) : p.name) + ": ";
    if (p.code.empty()) return tr("{0}codice vuoto", where);
    if (p.lines.size() != p.code.size() || p.columns.size() != p.code.size()) return tr("{0}informazioni di riga incoerenti", where);
    if (p.parameters > p.locals) return tr("{0}parametri oltre le variabili locali", where);
    if (p.minimum > p.parameters) return tr("{0}numero minimo di argomenti non valido", where);
    if (p.locals > 65536 || p.maxStack > 65536) return tr("{0}limiti di stack eccessivi", where);
    if (!p.defaults.empty() && p.defaults.size() != p.parameters) return tr("{0}valori predefiniti incoerenti", where);
    for (auto d : p.defaults)
        if (d != noDefault && d >= module.prototypes.size()) return tr("{0}valore predefinito fuori intervallo", where);
    if (!p.cellParameters.empty() && p.cellParameters.size() != p.parameters) return tr("{0}parametri catturati incoerenti", where);
    for (const auto& l : p.localNames)
        if (l.slot >= p.locals || l.from > p.code.size()) return tr("{0}informazioni di debug non valide", where);
    for (const auto& c : p.classes) {
        if (c.hasFieldInitializer && c.fieldInitializer >= module.prototypes.size()) return tr("{0}inizializzatore di classe non valido", where);
        if (c.mutableFields.size() != c.fields.size()) return tr("{0}campi di classe incoerenti", where);
        for (const auto& m : c.methods)
            if (m.second >= module.prototypes.size()) return tr("{0}metodo di classe non valido", where);
    }
    auto textConstant = [&](std::uint32_t k) { return k < p.constants.size() && p.constants[k].kind == Constant::Kind::text; };
    for (std::size_t pc = 0; pc < p.code.size(); ++pc) {
        const Instruction& i = p.code[pc];
        std::string at = where + "istruzione " + std::to_string(pc) + " ";
        if (static_cast<std::size_t>(i.op) >= static_cast<std::size_t>(Op::count)) return at + "codice operativo sconosciuto";
        switch (i.op) {
        case Op::constant: if (i.a >= p.constants.size()) return at + "costante fuori intervallo"; break;
        case Op::getLocal:
        case Op::setLocal:
        case Op::makeCell:
        case Op::getCell:
        case Op::setCell: if (i.a >= p.locals) return at + "variabile locale fuori intervallo"; break;
        case Op::getUpvalue:
        case Op::setUpvalue: if (i.a >= p.captures.size()) return at + "variabile catturata fuori intervallo"; break;
        case Op::getGlobal:
        case Op::setGlobal:
        case Op::defineGlobal:
        case Op::getMember:
        case Op::getMemberOptional:
        case Op::setMember:
        case Op::import:
        case Op::shorthand:
        case Op::isType:
        case Op::cast: if (!textConstant(i.a)) return tr("{0}nome non valido", at); break;
        case Op::call: if (i.a > 255) return tr("{0}troppi argomenti", at); break;
        case Op::callNamed:
            if (i.a > 255 || i.b >= p.constants.size() || p.constants[i.b].kind != Constant::Kind::names || p.constants[i.b].names.size() != i.a) return tr("{0}argomenti con nome non validi", at);
            break;
        case Op::makeList:
        case Op::makeMap:
        case Op::interpolate: if (i.a > 65535) return tr("{0}troppi elementi", at); break;
        case Op::iterNext: if (i.b < 1 || i.b > 2) return tr("{0}numero di valori del ciclo non valido", at); break;
        case Op::closure: {
            if (i.a >= module.prototypes.size()) return tr("{0}funzione fuori intervallo", at);
            for (const auto& capture : module.prototypes[i.a].captures) {
                if (capture.fromLocal && capture.index >= p.locals) return tr("{0}cattura di variabile locale non valida", at);
                if (!capture.fromLocal && capture.index >= p.captures.size()) return tr("{0}cattura di variabile esterna non valida", at);
            }
            break;
        }
        case Op::makeClass: if (i.a >= p.classes.size()) return tr("{0}classe fuori intervallo", at); break;
        case Op::makeEnum: if (i.a >= p.enums.size()) return at + "enum fuori intervallo"; break;
        case Op::makeRange: if (i.a > 1) return tr("{0}intervallo non valido", at); break;
        default: break;
        }
        if (isJump(i.op) && i.a >= p.code.size()) return tr("{0}salto fuori dal codice", at);
    }
    std::vector<int> depth(p.code.size(), -1);
    std::deque<std::pair<std::size_t, int>> work{{0, 0}};
    while (!work.empty()) {
        auto [pc, height] = work.front();
        work.pop_front();
        while (true) {
            if (pc >= p.code.size()) return tr("{0}il codice termina senza return", where);
            if (depth[pc] >= 0) {
                if (depth[pc] != height) return where + "altezza dello stack incoerente all'istruzione " + std::to_string(pc);
                break;
            }
            depth[pc] = height;
            const Instruction& i = p.code[pc];
            Effect e = effect(i);
            if (height < e.pops) return where + "stack insufficiente all'istruzione " + std::to_string(pc) + " (" + std::string(opName(i.op)) + ")";
            int after = height - e.pops + e.pushes;
            if (after > static_cast<int>(p.maxStack)) return tr("{0}stack oltre il massimo dichiarato all’istruzione {1}", where, std::to_string(pc));
            if (i.op == Op::returnValue || i.op == Op::returnNone) break;
            if (i.op == Op::jump) { pc = i.a; continue; }
            if (i.op == Op::iterNext) {
                work.emplace_back(i.a, height);
                height = after;
                ++pc;
                continue;
            }
            if (isJump(i.op)) work.emplace_back(i.a, after);
            height = after;
            ++pc;
        }
    }
    return std::nullopt;
}

}

std::string_view opName(Op op) {
    auto index = static_cast<std::size_t>(op);
    return index < static_cast<std::size_t>(Op::count) ? names[index] : "?";
}

std::string serialize(const Module& module) {
    Writer w;
    w.out += "LIMB";
    w.u16(bytecodeVersion);
    w.text(module.name);
    w.text(module.file);
    w.text(module.context);
    w.u32(module.main);
    w.u32(static_cast<std::uint32_t>(module.exports.size()));
    for (const auto& e : module.exports) w.text(e);
    w.u32(static_cast<std::uint32_t>(module.prototypes.size()));
    for (const auto& p : module.prototypes) {
        w.text(p.name);
        w.u32(p.parameters);
        w.u32(p.minimum);
        w.u32(p.locals);
        w.u32(p.maxStack);
        w.u8(static_cast<std::uint8_t>((p.async ? 1 : 0) | (p.method ? 2 : 0)));
        w.u32(static_cast<std::uint32_t>(p.parameterNames.size()));
        for (const auto& n : p.parameterNames) w.text(n);
        w.u32(static_cast<std::uint32_t>(p.defaults.size()));
        for (auto d : p.defaults) w.u32(d);
        w.u32(static_cast<std::uint32_t>(p.cellParameters.size()));
        for (bool c : p.cellParameters) w.u8(c ? 1 : 0);
        w.u32(static_cast<std::uint32_t>(p.localNames.size()));
        for (const auto& l : p.localNames) { w.u32(l.slot); w.u32(l.from); w.text(l.name); }
        w.u32(static_cast<std::uint32_t>(p.code.size()));
        for (std::size_t i = 0; i < p.code.size(); ++i) {
            w.u8(static_cast<std::uint8_t>(p.code[i].op));
            w.u32(p.code[i].a);
            w.u32(p.code[i].b);
            w.u32(p.lines[i]);
            w.u32(p.columns[i]);
        }
        w.u32(static_cast<std::uint32_t>(p.constants.size()));
        for (const auto& c : p.constants) {
            w.u8(static_cast<std::uint8_t>(c.kind));
            switch (c.kind) {
            case Constant::Kind::integer: w.u64(static_cast<std::uint64_t>(c.integer)); break;
            case Constant::Kind::number: w.f64(c.number); break;
            case Constant::Kind::text: w.text(c.text); break;
            case Constant::Kind::enumValue: w.text(c.enumValue.type); w.text(c.enumValue.name); break;
            case Constant::Kind::names:
                w.u32(static_cast<std::uint32_t>(c.names.size()));
                for (const auto& n : c.names) w.text(n);
                break;
            }
        }
        w.u32(static_cast<std::uint32_t>(p.captures.size()));
        for (const auto& c : p.captures) { w.u8(c.fromLocal ? 1 : 0); w.u32(c.index); }
        w.u32(static_cast<std::uint32_t>(p.classes.size()));
        for (const auto& c : p.classes) {
            w.text(c.name);
            w.u32(static_cast<std::uint32_t>(c.fields.size()));
            for (std::size_t f = 0; f < c.fields.size(); ++f) { w.text(c.fields[f]); w.u8(c.mutableFields[f] ? 1 : 0); }
            w.u8(c.hasFieldInitializer ? 1 : 0);
            w.u32(c.fieldInitializer);
            w.u32(static_cast<std::uint32_t>(c.methods.size()));
            for (const auto& [name, proto] : c.methods) { w.text(name); w.u32(proto); }
        }
        w.u32(static_cast<std::uint32_t>(p.enums.size()));
        for (const auto& e : p.enums) {
            w.text(e.name);
            w.u32(static_cast<std::uint32_t>(e.cases.size()));
            for (const auto& c : e.cases) w.text(c);
        }
    }
    return w.out;
}

LoadResult deserialize(std::string_view data) {
    LoadResult result;
    if (data.size() < 6 || data.substr(0, 4) != "LIMB") { result.error = tr("Bytecode non valido: intestazione mancante"); return result; }
    Reader r(data.substr(4));
    std::uint16_t version = r.u16();
    if (version != bytecodeVersion) { result.error = tr("Bytecode di una versione non supportata ({0})", std::to_string(version)); return result; }
    Module m;
    m.name = r.text();
    m.file = r.text();
    m.context = r.text();
    m.main = r.u32();
    for (std::uint32_t n = r.count(), i = 0; i < n && !r.failed; ++i) m.exports.push_back(r.text());
    for (std::uint32_t n = r.count(), i = 0; i < n && !r.failed; ++i) {
        Prototype p;
        p.name = r.text();
        p.parameters = r.u32();
        p.minimum = r.u32();
        p.locals = r.u32();
        p.maxStack = r.u32();
        std::uint8_t flags = r.u8();
        p.async = flags & 1;
        p.method = flags & 2;
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) p.parameterNames.push_back(r.text());
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) p.defaults.push_back(r.u32());
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) p.cellParameters.push_back(r.u8() != 0);
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            LocalName l;
            l.slot = r.u32();
            l.from = r.u32();
            l.name = r.text();
            p.localNames.push_back(std::move(l));
        }
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            Instruction ins;
            std::uint8_t op = r.u8();
            if (op >= static_cast<std::uint8_t>(Op::count)) { result.error = tr("Bytecode non valido: codice operativo sconosciuto"); return result; }
            ins.op = static_cast<Op>(op);
            ins.a = r.u32();
            ins.b = r.u32();
            p.code.push_back(ins);
            p.lines.push_back(r.u32());
            p.columns.push_back(r.u32());
        }
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            Constant k;
            std::uint8_t kind = r.u8();
            if (kind > static_cast<std::uint8_t>(Constant::Kind::names)) { result.error = tr("Bytecode non valido: costante sconosciuta"); return result; }
            k.kind = static_cast<Constant::Kind>(kind);
            switch (k.kind) {
            case Constant::Kind::integer: k.integer = static_cast<long long>(r.u64()); break;
            case Constant::Kind::number: k.number = r.f64(); break;
            case Constant::Kind::text: k.text = r.text(); break;
            case Constant::Kind::enumValue: k.enumValue.type = r.text(); k.enumValue.name = r.text(); break;
            case Constant::Kind::names:
                for (std::uint32_t q = r.count(), z = 0; z < q && !r.failed; ++z) k.names.push_back(r.text());
                break;
            }
            p.constants.push_back(std::move(k));
        }
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            Capture cap;
            cap.fromLocal = r.u8() != 0;
            cap.index = r.u32();
            p.captures.push_back(cap);
        }
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            ClassDescriptor d;
            d.name = r.text();
            for (std::uint32_t f = r.count(), z = 0; z < f && !r.failed; ++z) {
                d.fields.push_back(r.text());
                d.mutableFields.push_back(r.u8() != 0);
            }
            d.hasFieldInitializer = r.u8() != 0;
            d.fieldInitializer = r.u32();
            for (std::uint32_t f = r.count(), z = 0; z < f && !r.failed; ++z) {
                std::string name = r.text();
                d.methods.emplace_back(name, r.u32());
            }
            p.classes.push_back(std::move(d));
        }
        for (std::uint32_t c = r.count(), j = 0; j < c && !r.failed; ++j) {
            EnumDescriptor e;
            e.name = r.text();
            for (std::uint32_t f = r.count(), z = 0; z < f && !r.failed; ++z) e.cases.push_back(r.text());
            p.enums.push_back(std::move(e));
        }
        m.prototypes.push_back(std::move(p));
    }
    if (r.failed) { result.error = tr("Bytecode non valido: dati troncati o corrotti"); return result; }
    if (!r.atEnd()) { result.error = tr("Bytecode non valido: dati in eccesso"); return result; }
    if (auto problem = validate(m)) { result.error = tr("Bytecode non valido: {0}", *problem); return result; }
    result.module = std::move(m);
    return result;
}

std::optional<std::string> validate(const Module& module) {
    if (module.prototypes.empty()) return tr("nessuna funzione");
    if (module.main >= module.prototypes.size()) return tr("funzione principale fuori intervallo");
    if (!module.prototypes[module.main].captures.empty()) return tr("catture non ammesse nella funzione principale");
    for (std::size_t i = 0; i < module.prototypes.size(); ++i)
        if (auto problem = validatePrototype(module, i)) return problem;
    return std::nullopt;
}

std::uint32_t measureStack(const Prototype& p) {
    if (p.code.empty()) return 0;
    std::vector<int> depth(p.code.size(), -1);
    std::deque<std::pair<std::size_t, int>> work{{0, 0}};
    int maximum = 0;
    while (!work.empty()) {
        auto [pc, height] = work.front();
        work.pop_front();
        while (pc < p.code.size() && depth[pc] < 0) {
            depth[pc] = height;
            const Instruction& i = p.code[pc];
            Effect e = effect(i);
            int after = std::max(0, height - e.pops) + e.pushes;
            maximum = std::max(maximum, std::max(height, after));
            if (i.op == Op::returnValue || i.op == Op::returnNone) break;
            if (i.op == Op::jump) { pc = i.a; continue; }
            if (i.op == Op::iterNext) work.emplace_back(i.a, height);
            else if (isJump(i.op)) work.emplace_back(i.a, after);
            height = after;
            ++pc;
        }
    }
    return static_cast<std::uint32_t>(maximum);
}

}
