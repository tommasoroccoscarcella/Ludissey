#include "Lim/VM/Vm.hpp"

#include "Localization/Localization.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <xlocale.h>

namespace lim {

using ld::tr;

namespace {

constexpr double pi = 3.14159265358979323846;

locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

double number(const Value& v, const std::string& what) {
    if (v.numeric()) return v.toNumber();
    throw ScriptError(tr("{0}: serve un numero, trovato {1}", what, kindName(v)));
}

double numberOr(const Value& v, double fallback, const std::string& what) {
    if (v.isNone()) return fallback;
    return number(v, what);
}

long long integer(const Value& v, const std::string& what) {
    if (v.kind == ValueKind::integer) return v.integer;
    if (v.kind == ValueKind::number && std::floor(v.number) == v.number && std::abs(v.number) < 9e15) return static_cast<long long>(v.number);
    throw ScriptError(tr("{0}: serve un intero, trovato {1}", what, kindName(v)));
}

const std::string& textArgument(Vm& vm, const Value& v, const std::string& what) {
    if (v.kind != ValueKind::text) throw ScriptError(tr("{0}: serve un testo, trovato {1}", what, kindName(v)));
    return vm.textOf(v);
}

const DataObject& data(const Value& v, DataKind kind, const std::string& what) {
    if (!isData(v, kind)) throw ScriptError(tr("{0}: serve {1}, trovato {2}", what, std::string(dataName(kind)), kindName(v)));
    return *v.as<DataObject>();
}

std::array<std::array<double, 3>, 3> rotationMatrix(double x, double y, double z) {
    auto axis = [](int which, double degrees) {
        double a = degrees * pi / 180, c = std::cos(a), s = std::sin(a);
        std::array<std::array<double, 3>, 3> r{};
        if (which == 0) r = {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
        else if (which == 1) r = {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
        else r = {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
        return r;
    };
    auto multiply = [](const auto& a, const auto& b) {
        std::array<std::array<double, 3>, 3> r{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = a[static_cast<std::size_t>(i)][0] * b[0][static_cast<std::size_t>(j)] + a[static_cast<std::size_t>(i)][1] * b[1][static_cast<std::size_t>(j)] + a[static_cast<std::size_t>(i)][2] * b[2][static_cast<std::size_t>(j)];
        return r;
    };
    return multiply(axis(2, z), multiply(axis(1, y), axis(0, x)));
}

std::vector<std::string> characters(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t start = i++;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
        out.push_back(text.substr(start, i - start));
    }
    return out;
}

std::string hexOf(double r, double g, double b) {
    auto channel = [](double v) { return static_cast<int>(std::round(std::clamp(v, 0.0, 1.0) * 255)); };
    char buffer[8];
    snprintf(buffer, sizeof buffer, "%02X%02X%02X", channel(r), channel(g), channel(b));
    return buffer;
}

Value fromJson(Vm& vm, const std::string& text, std::size_t& position, int depth) {
    auto skip = [&] { while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) ++position; };
    skip();
    if (depth > 64) throw ScriptError(tr("JSON troppo annidato"));
    if (position >= text.size()) throw ScriptError("JSON incompleto");
    char c = text[position];
    if (c == '{') {
        ++position;
        Value m = vm.map();
        skip();
        if (position < text.size() && text[position] == '}') { ++position; return m; }
        while (true) {
            skip();
            Value key = fromJson(vm, text, position, depth + 1);
            if (key.kind != ValueKind::text) throw ScriptError(tr("Chiave JSON non valida"));
            skip();
            if (position >= text.size() || text[position] != ':') throw ScriptError(tr("JSON non valido: mancano i due punti"));
            ++position;
            Value value = fromJson(vm, text, position, depth + 1);
            m.as<MapObject>()->set(key, value);
            vm.charge(*m.object, 48);
            skip();
            if (position < text.size() && text[position] == ',') { ++position; continue; }
            if (position < text.size() && text[position] == '}') { ++position; return m; }
            throw ScriptError(tr("JSON non valido: oggetto non chiuso"));
        }
    }
    if (c == '[') {
        ++position;
        std::vector<Value> items;
        skip();
        if (position < text.size() && text[position] == ']') { ++position; return vm.list(); }
        while (true) {
            items.push_back(fromJson(vm, text, position, depth + 1));
            skip();
            if (position < text.size() && text[position] == ',') { ++position; continue; }
            if (position < text.size() && text[position] == ']') { ++position; return vm.list(std::move(items)); }
            throw ScriptError(tr("JSON non valido: lista non chiusa"));
        }
    }
    if (c == '"') {
        ++position;
        std::string out;
        while (position < text.size() && text[position] != '"') {
            char d = text[position++];
            if (d != '\\') { out += d; continue; }
            if (position >= text.size()) break;
            char e = text[position++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (position + 4 > text.size()) throw ScriptError(tr("JSON non valido"));
                unsigned code = static_cast<unsigned>(std::strtoul(text.substr(position, 4).c_str(), nullptr, 16));
                position += 4;
                if (code < 0x80) out += static_cast<char>(code);
                else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                else { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                break;
            }
            default: out += e;
            }
        }
        if (position >= text.size()) throw ScriptError(tr("JSON non valido: testo non chiuso"));
        ++position;
        return vm.text(out);
    }
    if (text.compare(position, 4, "true") == 0) { position += 4; return Value::boolean(true); }
    if (text.compare(position, 5, "false") == 0) { position += 5; return Value::boolean(false); }
    if (text.compare(position, 4, "null") == 0) { position += 4; return Value(); }
    std::size_t start = position;
    while (position < text.size() && (std::isdigit(static_cast<unsigned char>(text[position])) || text[position] == '-' || text[position] == '+' || text[position] == '.' || text[position] == 'e' || text[position] == 'E')) ++position;
    if (start == position) throw ScriptError(tr("JSON non valido"));
    std::string digits = text.substr(start, position - start);
    char* end = nullptr;
    double parsed = strtod_l(digits.c_str(), &end, cLocale());
    if (end != digits.c_str() + digits.size()) throw ScriptError(tr("Numero JSON non valido"));
    if (digits.find_first_of(".eE") == std::string::npos && std::abs(parsed) < 9e15) return Value::from(static_cast<long long>(parsed));
    return Value::real(parsed);
}

std::string escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else out += c;
        }
    }
    return out + "\"";
}

Value namespaceObject(Vm& vm, const std::string& name, std::map<std::string, Value> members, NativeFunction call = {}) {
    auto n = std::make_shared<NativeObject>();
    n->name = name;
    n->members = std::move(members);
    n->function = call ? std::move(call) : NativeFunction([name](NativeCall&) -> NativeResult { throw ScriptError(tr("{0} non si può chiamare", name)); });
    return Value::make(ValueKind::native, n);
}

Value randomObject(Vm& vm, std::shared_ptr<std::mt19937_64> engine) {
    std::map<std::string, Value> members;
    members["next"] = vm.native("next", [engine](NativeCall&) { return NativeResult::of(Value::real(std::uniform_real_distribution<double>(0, 1)(*engine))); });
    members["integer"] = vm.native("integer", [engine](NativeCall& c) {
        long long low = integer(c.parameter(0, "minimo"), "Random.integer"), high = integer(c.parameter(1, "massimo"), "Random.integer");
        if (low > high) throw ScriptError(tr("Random.integer: il minimo supera il massimo"));
        return NativeResult::of(Value::from(std::uniform_int_distribution<long long>(low, high)(*engine)));
    });
    members["number"] = vm.native("number", [engine](NativeCall& c) {
        double low = number(c.parameter(0, "minimo"), "Random.number"), high = number(c.parameter(1, "massimo"), "Random.number");
        return NativeResult::of(Value::real(low + (high - low) * std::uniform_real_distribution<double>(0, 1)(*engine)));
    });
    return namespaceObject(vm, "Random", std::move(members));
}

}

void installLibrary(Vm& vm) {
    auto joined = [](NativeCall& c) {
        std::string out;
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (i) out += " ";
            out += c.vm.toText(c.argument(i));
        }
        return out;
    };
    vm.defineBuiltin("print", vm.native("print", [joined](NativeCall& c) {
        c.vm.emit(Severity::hint, joined(c), c.fiber);
        return NativeResult::of(Value());
    }));
    vm.defineBuiltin("warn", vm.native("warn", [joined](NativeCall& c) {
        c.vm.emit(Severity::warning, joined(c), c.fiber);
        return NativeResult::of(Value());
    }));
    vm.defineBuiltin("error", vm.native("error", [](NativeCall& c) -> NativeResult { throw ScriptError(c.vm.toText(c.argument(0))); }));
    vm.defineBuiltin("typeOf", vm.native("typeOf", [](NativeCall& c) { return NativeResult::of(c.vm.text(kindName(c.argument(0)))); }));
    vm.defineBuiltin("Text", vm.native("Text", [](NativeCall& c) { return NativeResult::of(c.vm.text(c.vm.toText(c.argument(0)))); }));
    vm.defineBuiltin("Number", vm.native("Number", [](NativeCall& c) {
        const Value& v = c.argument(0);
        if (v.numeric()) return NativeResult::of(Value::real(v.toNumber()));
        if (v.kind == ValueKind::text) {
            std::string s = c.vm.textOf(v);
            char* end = nullptr;
            double parsed = strtod_l(s.c_str(), &end, cLocale());
            if (!s.empty() && end == s.c_str() + s.size() && std::isfinite(parsed)) return NativeResult::of(Value::real(parsed));
        }
        return NativeResult::of(Value());
    }));
    vm.defineBuiltin("Integer", vm.native("Integer", [](NativeCall& c) {
        const Value& v = c.argument(0);
        if (v.kind == ValueKind::integer) return NativeResult::of(v);
        if (v.kind == ValueKind::number && std::isfinite(v.number) && std::abs(v.number) < 9e18) return NativeResult::of(Value::from(static_cast<long long>(v.number)));
        if (v.kind == ValueKind::text) {
            std::string s = c.vm.textOf(v);
            char* end = nullptr;
            long long parsed = std::strtoll(s.c_str(), &end, 10);
            if (!s.empty() && end == s.c_str() + s.size()) return NativeResult::of(Value::from(parsed));
        }
        return NativeResult::of(Value());
    }));

    std::map<std::string, Value> math;
    auto unary = [&vm, &math](const std::string& name, double (*f)(double)) {
        math[name] = vm.native(name, [name, f](NativeCall& c) { return NativeResult::of(Value::real(f(number(c.argument(0), "Math." + name)))); });
    };
    unary("sqrt", std::sqrt);
    unary("sin", std::sin);
    unary("cos", std::cos);
    unary("tan", std::tan);
    unary("asin", std::asin);
    unary("acos", std::acos);
    unary("atan", std::atan);
    unary("exp", std::exp);
    unary("log", std::log);
    auto integral = [&vm, &math](const std::string& name, double (*f)(double)) {
        math[name] = vm.native(name, [name, f](NativeCall& c) {
            const Value& v = c.argument(0);
            if (v.kind == ValueKind::integer) return NativeResult::of(v);
            double r = f(number(v, "Math." + name));
            if (std::abs(r) < 9e15) return NativeResult::of(Value::from(static_cast<long long>(r)));
            return NativeResult::of(Value::real(r));
        });
    };
    integral("floor", std::floor);
    integral("ceil", std::ceil);
    integral("round", std::round);
    math["abs"] = vm.native("abs", [](NativeCall& c) {
        const Value& v = c.argument(0);
        if (v.kind == ValueKind::integer) {
            if (v.integer == std::numeric_limits<long long>::min()) return NativeResult::of(Value::real(-static_cast<double>(v.integer)));
            return NativeResult::of(Value::from(v.integer < 0 ? -v.integer : v.integer));
        }
        return NativeResult::of(Value::real(std::abs(number(v, "Math.abs"))));
    });
    math["sign"] = vm.native("sign", [](NativeCall& c) {
        double x = number(c.argument(0), "Math.sign");
        return NativeResult::of(Value::from(x > 0 ? 1 : x < 0 ? -1 : 0));
    });
    math["atan2"] = vm.native("atan2", [](NativeCall& c) { return NativeResult::of(Value::real(std::atan2(number(c.argument(0), "Math.atan2"), number(c.argument(1), "Math.atan2")))); });
    math["pow"] = vm.native("pow", [](NativeCall& c) { return NativeResult::of(Value::real(std::pow(number(c.argument(0), "Math.pow"), number(c.argument(1), "Math.pow")))); });
    auto pick = [](bool smaller) {
        return [smaller](NativeCall& c) {
            const Value& a = c.argument(0);
            const Value& b = c.argument(1);
            if (a.kind == ValueKind::integer && b.kind == ValueKind::integer) return NativeResult::of(Value::from(smaller ? std::min(a.integer, b.integer) : std::max(a.integer, b.integer)));
            double x = number(a, "Math"), y = number(b, "Math");
            return NativeResult::of(Value::real(smaller ? std::min(x, y) : std::max(x, y)));
        };
    };
    math["min"] = vm.native("min", pick(true));
    math["max"] = vm.native("max", pick(false));
    math["clamp"] = vm.native("clamp", [](NativeCall& c) {
        double x = number(c.argument(0), "Math.clamp"), low = number(c.argument(1), "Math.clamp"), high = number(c.argument(2), "Math.clamp");
        if (low > high) throw ScriptError(tr("Math.clamp: il minimo supera il massimo"));
        if (c.argument(0).kind == ValueKind::integer && c.argument(1).kind == ValueKind::integer && c.argument(2).kind == ValueKind::integer)
            return NativeResult::of(Value::from(std::clamp(c.argument(0).integer, c.argument(1).integer, c.argument(2).integer)));
        return NativeResult::of(Value::real(std::clamp(x, low, high)));
    });
    math["lerp"] = vm.native("lerp", [](NativeCall& c) {
        double a = number(c.argument(0), "Math.lerp"), b = number(c.argument(1), "Math.lerp"), t = number(c.argument(2), "Math.lerp");
        return NativeResult::of(Value::real(a + (b - a) * t));
    });
    math["pi"] = Value::real(pi);
    math["huge"] = Value::real(HUGE_VAL);
    vm.defineBuiltin("Math", namespaceObject(vm, "Math", std::move(math)));

    auto shared = std::make_shared<std::mt19937_64>(std::random_device{}());
    Value statics = randomObject(vm, shared);
    std::map<std::string, Value> randomMembers = statics.as<NativeObject>()->members;
    vm.defineBuiltin("Random", namespaceObject(vm, "Random", randomMembers, [](NativeCall& c) {
        auto engine = std::make_shared<std::mt19937_64>(c.argument(0).isNone() ? std::random_device{}() : static_cast<std::uint64_t>(integer(c.argument(0), "Random")));
        return NativeResult::of(randomObject(c.vm, engine));
    }));

    std::map<std::string, Value> time;
    time["now"] = vm.native("now", [](NativeCall&) {
        return NativeResult::of(Value::real(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()));
    });
    time["clock"] = vm.native("clock", [](NativeCall& c) { return NativeResult::of(Value::real(c.vm.clock())); });
    vm.defineBuiltin("Time", namespaceObject(vm, "Time", std::move(time)));

    std::map<std::string, Value> task;
    task["wait"] = vm.native("wait", [](NativeCall& c) {
        double seconds = numberOr(c.parameter(0, "secondi"), 0, "Task.wait");
        if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
        NativeResult r;
        r.suspension.kind = seconds <= 0 ? Suspension::Kind::frame : Suspension::Kind::time;
        r.suspension.until = c.vm.clock() + seconds;
        return r;
    }, true);
    task["nextFrame"] = vm.native("nextFrame", [](NativeCall&) {
        NativeResult r;
        r.suspension.kind = Suspension::Kind::frame;
        return r;
    }, true);
    task["spawn"] = vm.native("spawn", [](NativeCall& c) {
        const Value& function = c.parameter(0, "funzione");
        if (function.kind != ValueKind::function && function.kind != ValueKind::native && function.kind != ValueKind::boundMethod) throw ScriptError(tr("Task.spawn: serve una funzione"));
        auto fiber = c.vm.spawn(function, {}, c.fiber ? c.fiber->label : std::string(tr("attività")));
        return NativeResult::of(Value::make(ValueKind::task, fiber->task));
    });
    task["delay"] = vm.native("delay", [](NativeCall& c) {
        double seconds = numberOr(c.parameter(0, "secondi"), 0, "Task.delay");
        Value function = c.parameter(1, "funzione");
        if (function.kind != ValueKind::function && function.kind != ValueKind::native && function.kind != ValueKind::boundMethod) throw ScriptError(tr("Task.delay: serve una funzione"));
        Suspension s;
        s.kind = Suspension::Kind::time;
        s.until = c.vm.clock() + std::max(0.0, seconds);
        Vm* vm = &c.vm;
        std::string label = c.fiber ? c.fiber->label : std::string(tr("attività"));
        return NativeResult::of(c.vm.suspend(std::move(s), [vm, function, label](Value) { vm->spawn(function, {}, label); }));
    });
    task["cancel"] = vm.native("cancel", [](NativeCall& c) {
        const Value& t = c.argument(0);
        if (t.kind != ValueKind::task) throw ScriptError(tr("Task.cancel: serve un’attività"));
        c.vm.cancelTask(std::static_pointer_cast<TaskObject>(t.object));
        return NativeResult::of(Value());
    });
    vm.defineBuiltin("Task", namespaceObject(vm, "Task", std::move(task)));

    std::map<std::string, Value> json;
    json["encode"] = vm.native("encode", [](NativeCall& c) { return NativeResult::of(c.vm.text(c.vm.fromValueToJson(c.argument(0)))); });
    json["decode"] = vm.native("decode", [](NativeCall& c) { return NativeResult::of(c.vm.toJsonValue(textArgument(c.vm, c.argument(0), "JSON.decode"))); });
    vm.defineBuiltin("JSON", namespaceObject(vm, "JSON", std::move(json)));

    std::map<std::string, Value> vec3;
    vec3["zero"] = makeVec3(0, 0, 0);
    vec3["one"] = makeVec3(1, 1, 1);
    vec3["up"] = makeVec3(0, 1, 0);
    vm.defineBuiltin("Vec3", namespaceObject(vm, "Vec3", std::move(vec3), [](NativeCall& c) {
        return NativeResult::of(makeVec3(numberOr(c.parameter(0, "x"), 0, "Vec3"), numberOr(c.parameter(1, "y"), 0, "Vec3"), numberOr(c.parameter(2, "z"), 0, "Vec3")));
    }));
    std::map<std::string, Value> vec2;
    vec2["zero"] = makeVec2(0, 0);
    vm.defineBuiltin("Vec2", namespaceObject(vm, "Vec2", std::move(vec2), [](NativeCall& c) {
        return NativeResult::of(makeVec2(numberOr(c.parameter(0, "x"), 0, "Vec2"), numberOr(c.parameter(1, "y"), 0, "Vec2")));
    }));
    std::map<std::string, Value> color;
    color["white"] = makeColor(1, 1, 1);
    color["black"] = makeColor(0, 0, 0);
    color["red"] = makeColor(1, 0, 0);
    color["green"] = makeColor(0, 1, 0);
    color["blue"] = makeColor(0, 0, 1);
    color["yellow"] = makeColor(1, 1, 0);
    color["rgb"] = vm.native("rgb", [](NativeCall& c) {
        return NativeResult::of(makeColor(number(c.parameter(0, "r"), "Color.rgb"), number(c.parameter(1, "g"), "Color.rgb"), number(c.parameter(2, "b"), "Color.rgb")));
    });
    color["bytes"] = vm.native("bytes", [](NativeCall& c) {
        return NativeResult::of(makeColor(number(c.parameter(0, "r"), "Color.bytes") / 255, number(c.parameter(1, "g"), "Color.bytes") / 255, number(c.parameter(2, "b"), "Color.bytes") / 255));
    });
    color["hex"] = vm.native("hex", [](NativeCall& c) {
        std::string text = textArgument(c.vm, c.argument(0), "Color.hex");
        if (!text.empty() && text[0] == '#') text = text.substr(1);
        if (text.size() != 6 || !std::all_of(text.begin(), text.end(), [](char ch) { return std::isxdigit(static_cast<unsigned char>(ch)); })) throw ScriptError(tr("Color.hex: serve un colore come \"6633EE\""));
        unsigned long v = std::strtoul(text.c_str(), nullptr, 16);
        return NativeResult::of(makeColor(static_cast<double>((v >> 16) & 255) / 255, static_cast<double>((v >> 8) & 255) / 255, static_cast<double>(v & 255) / 255));
    });
    vm.defineBuiltin("Color", namespaceObject(vm, "Color", std::move(color), [](NativeCall& c) {
        return NativeResult::of(makeColor(numberOr(c.parameter(0, "r"), 0, "Color"), numberOr(c.parameter(1, "g"), 0, "Color"), numberOr(c.parameter(2, "b"), 0, "Color")));
    }));
    vm.defineBuiltin("Rotation", namespaceObject(vm, "Rotation", {}, [](NativeCall& c) {
        return NativeResult::of(makeData(DataKind::rotation, {numberOr(c.parameter(0, "x"), 0, "Rotation"), numberOr(c.parameter(1, "y"), 0, "Rotation"), numberOr(c.parameter(2, "z"), 0, "Rotation")}));
    }));
    vm.defineBuiltin("Transform", namespaceObject(vm, "Transform", {}, [](NativeCall& c) {
        const auto& p = data(c.parameter(0, "position"), DataKind::vec3, "Transform");
        std::array<double, 3> r{0, 0, 0};
        if (!c.parameter(1, "rotation").isNone()) {
            const auto& rot = data(c.parameter(1, "rotation"), DataKind::rotation, "Transform");
            r = {rot.values[0], rot.values[1], rot.values[2]};
        }
        return NativeResult::of(makeData(DataKind::transform, {p.values[0], p.values[1], p.values[2], r[0], r[1], r[2]}));
    }));
    vm.defineBuiltin("Ray", namespaceObject(vm, "Ray", {}, [](NativeCall& c) {
        const auto& o = data(c.parameter(0, "origin"), DataKind::vec3, "Ray");
        const auto& d = data(c.parameter(1, "direction"), DataKind::vec3, "Ray");
        return NativeResult::of(makeData(DataKind::ray, {o.values[0], o.values[1], o.values[2], d.values[0], d.values[1], d.values[2]}));
    }));
    vm.defineBuiltin("Bounds", namespaceObject(vm, "Bounds", {}, [](NativeCall& c) {
        const auto& l = data(c.parameter(0, "low"), DataKind::vec3, "Bounds");
        const auto& h = data(c.parameter(1, "high"), DataKind::vec3, "Bounds");
        return NativeResult::of(makeData(DataKind::bounds, {l.values[0], l.values[1], l.values[2], h.values[0], h.values[1], h.values[2]}));
    }));
    vm.defineBuiltin("Dim", namespaceObject(vm, "Dim", {}, [](NativeCall& c) {
        return NativeResult::of(makeData(DataKind::dim, {numberOr(c.parameter(0, "scale"), 0, "Dim"), numberOr(c.parameter(1, "offset"), 0, "Dim")}));
    }));
    std::map<std::string, Value> dim2;
    dim2["pixels"] = vm.native("pixels", [](NativeCall& c) { return NativeResult::of(makeData(DataKind::dim2, {0, number(c.parameter(0, "x"), "Dim2.pixels"), 0, number(c.parameter(1, "y"), "Dim2.pixels")})); });
    dim2["scale"] = vm.native("scale", [](NativeCall& c) { return NativeResult::of(makeData(DataKind::dim2, {number(c.parameter(0, "x"), "Dim2.scale"), 0, number(c.parameter(1, "y"), "Dim2.scale"), 0})); });
    vm.defineBuiltin("Dim2", namespaceObject(vm, "Dim2", std::move(dim2), [](NativeCall& c) {
        return NativeResult::of(makeData(DataKind::dim2, {numberOr(c.parameter(0, "xScale"), 0, "Dim2"), numberOr(c.parameter(1, "xOffset"), 0, "Dim2"), numberOr(c.parameter(2, "yScale"), 0, "Dim2"), numberOr(c.parameter(3, "yOffset"), 0, "Dim2")}));
    }));
}

Value Vm::toJsonValue(const std::string& input) {
    std::size_t position = 0;
    Value v = fromJson(*this, input, position, 0);
    while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) ++position;
    if (position != input.size()) throw ScriptError(tr("JSON non valido: caratteri in eccesso"));
    return v;
}

std::string Vm::fromValueToJson(const Value& v, int depth) {
    if (depth > 64) throw ScriptError(tr("Valore troppo annidato per JSON"));
    switch (v.kind) {
    case ValueKind::none: return "null";
    case ValueKind::boolean: return v.flag ? "true" : "false";
    case ValueKind::integer: return std::to_string(v.integer);
    case ValueKind::number: {
        if (!std::isfinite(v.number)) throw ScriptError(tr("JSON non ammette infinito o nan"));
        char buffer[40];
        snprintf_l(buffer, sizeof buffer, cLocale(), "%.17g", v.number);
        return buffer;
    }
    case ValueKind::text: return escape(v.as<TextObject>()->text);
    case ValueKind::list: {
        std::string out = "[";
        const auto& items = v.as<ListObject>()->items;
        for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + fromValueToJson(items[i], depth + 1);
        return out + "]";
    }
    case ValueKind::map: {
        std::string out = "{";
        const auto& entries = v.as<MapObject>()->entries;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].first.kind != ValueKind::text) throw ScriptError(tr("JSON richiede chiavi di testo"));
            out += (i ? "," : "") + escape(entries[i].first.as<TextObject>()->text) + ":" + fromValueToJson(entries[i].second, depth + 1);
        }
        return out + "}";
    }
    case ValueKind::enumValue: return escape(v.as<EnumValueObject>()->name);
    default: throw ScriptError(tr("{0} non si può convertire in JSON", kindName(v)));
    }
}

Value Vm::getMember(const Value& object, const std::string& name) {
    switch (object.kind) {
    case ValueKind::none: throw ScriptError(tr("Impossibile leggere «{0}» da none: il valore non esiste", name));
    case ValueKind::host:
        if (!hostInterface) break;
        return hostInterface->getMember(*this, object, name);
    case ValueKind::instance: {
        auto* instance = object.as<InstanceObject>();
        auto field = instance->type->fieldIndex.find(name);
        if (field != instance->type->fieldIndex.end()) return instance->fields[field->second];
        auto method = instance->type->methodIndex.find(name);
        if (method != instance->type->methodIndex.end()) {
            auto closure = std::make_shared<ClosureObject>();
            closure->module = instance->type->module;
            closure->prototype = method->second;
            closure->environment = current && !current->frames.empty() ? current->frames.back().environment : nullptr;
            auto bound = std::make_shared<BoundMethodObject>();
            bound->receiver = object;
            bound->function = Value::make(ValueKind::function, closure);
            register_(bound);
            return Value::make(ValueKind::boundMethod, bound);
        }
        throw ScriptError(tr("«{0}» non è un membro di {1}", name, instance->type->descriptor.name));
    }
    case ValueKind::native: {
        auto* native = object.as<NativeObject>();
        auto it = native->members.find(name);
        if (it != native->members.end()) return it->second;
        throw ScriptError(tr("«{0}» non è un membro di {1}", name, native->name));
    }
    case ValueKind::module: {
        auto* module = object.as<ModuleObject>();
        auto it = module->exports.find(name);
        if (it != module->exports.end()) return it->second;
        throw ScriptError(tr("Il modulo «{0}» non esporta «{1}»", module->name, name));
    }
    case ValueKind::enumType: {
        auto* type = object.as<EnumTypeObject>();
        if (std::find(type->cases.begin(), type->cases.end(), name) != type->cases.end()) return makeEnum(type->name, name);
        if (name == "all") {
            std::vector<Value> all;
            for (const auto& c : type->cases) all.push_back(makeEnum(type->name, c));
            return list(std::move(all));
        }
        throw ScriptError(tr("«{0}» non è un caso di {1}", name, type->name));
    }
    case ValueKind::enumValue: {
        auto* e = object.as<EnumValueObject>();
        if (name == "name") return text(e->name);
        if (name == "type") return text(e->type);
        break;
    }
    case ValueKind::task: {
        auto task = std::static_pointer_cast<TaskObject>(object.object);
        if (name == "done") return Value::boolean(task->done);
        if (name == "cancel") return boundNative("cancel", Value(), [task](NativeCall& c) { c.vm.cancelTask(task); return NativeResult::of(Value()); });
        break;
    }
    case ValueKind::range: {
        auto* r = object.as<RangeObject>();
        if (name == "count") return Value::from(std::max(0LL, r->high - r->low + (r->inclusive ? 1 : 0)));
        break;
    }
    case ValueKind::text: {
        const std::string& t = object.as<TextObject>()->text;
        if (name == "count") return Value::from(static_cast<long long>(characters(t).size()));
        if (name == "isEmpty") return Value::boolean(t.empty());
        Value self = object;
        auto method = [&](const char* label, NativeFunction f) { return boundNative(label, Value(), std::move(f)); };
        if (name == "upper" || name == "lower") {
            bool up = name == "upper";
            return method(name.c_str(), [self, up](NativeCall& c) {
                std::string s = c.vm.textOf(self);
                for (char& ch : s) ch = static_cast<char>(up ? std::toupper(static_cast<unsigned char>(ch)) : std::tolower(static_cast<unsigned char>(ch)));
                return NativeResult::of(c.vm.text(s));
            });
        }
        if (name == "trimmed") return method("trimmed", [self](NativeCall& c) {
            const std::string& s = c.vm.textOf(self);
            std::size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return NativeResult::of(c.vm.text(s.substr(a, b - a)));
        });
        if (name == "contains" || name == "startsWith" || name == "endsWith") {
            std::string which = name;
            return method(name.c_str(), [self, which](NativeCall& c) {
                const std::string& s = c.vm.textOf(self);
                const std::string& needle = textArgument(c.vm, c.argument(0), which);
                bool r = which == "contains" ? s.find(needle) != std::string::npos : which == "startsWith" ? s.rfind(needle, 0) == 0 : s.size() >= needle.size() && s.compare(s.size() - needle.size(), needle.size(), needle) == 0;
                return NativeResult::of(Value::boolean(r));
            });
        }
        if (name == "split") return method("split", [self](NativeCall& c) {
            const std::string& s = c.vm.textOf(self);
            std::string separator = c.argument(0).isNone() ? std::string(" ") : textArgument(c.vm, c.argument(0), "split");
            std::vector<Value> parts;
            if (separator.empty()) {
                for (auto& ch : characters(s)) parts.push_back(c.vm.text(ch));
            } else {
                std::size_t start = 0;
                while (true) {
                    auto next = s.find(separator, start);
                    if (next == std::string::npos) { parts.push_back(c.vm.text(s.substr(start))); break; }
                    parts.push_back(c.vm.text(s.substr(start, next - start)));
                    start = next + separator.size();
                }
            }
            return NativeResult::of(c.vm.list(std::move(parts)));
        });
        if (name == "replace") return method("replace", [self](NativeCall& c) {
            std::string s = c.vm.textOf(self);
            const std::string& from = textArgument(c.vm, c.parameter(0, "cerca"), "replace");
            const std::string& to = textArgument(c.vm, c.parameter(1, "con"), "replace");
            if (!from.empty()) {
                std::size_t position = 0;
                while ((position = s.find(from, position)) != std::string::npos) {
                    s.replace(position, from.size(), to);
                    position += to.size();
                }
            }
            return NativeResult::of(c.vm.text(s));
        });
        if (name == "find") return method("find", [self](NativeCall& c) {
            const std::string& s = c.vm.textOf(self);
            auto at = s.find(textArgument(c.vm, c.argument(0), "find"));
            if (at == std::string::npos) return NativeResult::of(Value());
            return NativeResult::of(Value::from(static_cast<long long>(characters(s.substr(0, at)).size())));
        });
        if (name == "slice") return method("slice", [self](NativeCall& c) {
            auto chars = characters(c.vm.textOf(self));
            long long from = integer(c.parameter(0, "da"), "slice");
            long long to = c.parameter(1, "a").isNone() ? static_cast<long long>(chars.size()) : integer(c.parameter(1, "a"), "slice");
            from = std::clamp<long long>(from, 0, static_cast<long long>(chars.size()));
            to = std::clamp<long long>(to, from, static_cast<long long>(chars.size()));
            std::string out;
            for (long long i = from; i < to; ++i) out += chars[static_cast<std::size_t>(i)];
            return NativeResult::of(c.vm.text(out));
        });
        if (name == "repeat") return method("repeat", [self](NativeCall& c) {
            long long times = integer(c.argument(0), "repeat");
            const std::string& s = c.vm.textOf(self);
            if (times < 0 || static_cast<double>(times) * static_cast<double>(s.size()) > 64.0 * 1024 * 1024) throw ScriptError(tr("repeat: risultato troppo grande"));
            std::string out;
            out.reserve(static_cast<std::size_t>(times) * s.size());
            for (long long i = 0; i < times; ++i) out += s;
            return NativeResult::of(c.vm.text(out));
        });
        break;
    }
    case ValueKind::list: {
        auto listObject = std::static_pointer_cast<ListObject>(object.object);
        auto& items = listObject->items;
        if (name == "count") return Value::from(static_cast<long long>(items.size()));
        if (name == "isEmpty") return Value::boolean(items.empty());
        if (name == "first") return items.empty() ? Value() : items.front();
        if (name == "last") return items.empty() ? Value() : items.back();
        auto method = [&](const char* label, NativeFunction f) { return boundNative(label, Value(), std::move(f)); };
        if (name == "append") return method("append", [listObject](NativeCall& c) {
            c.vm.charge(*listObject, 24);
            listObject->items.push_back(c.argument(0));
            return NativeResult::of(Value());
        });
        if (name == "insert") return method("insert", [listObject](NativeCall& c) {
            long long at = integer(c.parameter(1, "at"), "insert");
            if (at < 0 || static_cast<std::size_t>(at) > listObject->items.size()) throw ScriptError(tr("insert: posizione {0} fuori dalla lista", std::to_string(at)));
            c.vm.charge(*listObject, 24);
            listObject->items.insert(listObject->items.begin() + at, c.parameter(0, "elemento"));
            return NativeResult::of(Value());
        });
        if (name == "remove") return method("remove", [listObject](NativeCall& c) {
            long long at = integer(c.parameter(0, "at"), "remove");
            if (at < 0 || static_cast<std::size_t>(at) >= listObject->items.size()) throw ScriptError(tr("remove: posizione {0} fuori dalla lista", std::to_string(at)));
            Value removed = listObject->items[static_cast<std::size_t>(at)];
            listObject->items.erase(listObject->items.begin() + at);
            return NativeResult::of(removed);
        });
        if (name == "removeLast") return method("removeLast", [listObject](NativeCall&) {
            if (listObject->items.empty()) return NativeResult::of(Value());
            Value removed = listObject->items.back();
            listObject->items.pop_back();
            return NativeResult::of(removed);
        });
        if (name == "clear") return method("clear", [listObject](NativeCall&) { listObject->items.clear(); return NativeResult::of(Value()); });
        if (name == "contains" || name == "indexOf") {
            bool index = name == "indexOf";
            return method(name.c_str(), [listObject, index](NativeCall& c) {
                for (std::size_t i = 0; i < listObject->items.size(); ++i)
                    if (valuesEqual(listObject->items[i], c.argument(0))) return NativeResult::of(index ? Value::from(static_cast<long long>(i)) : Value::boolean(true));
                return NativeResult::of(index ? Value() : Value::boolean(false));
            });
        }
        if (name == "map" || name == "filter" || name == "forEach") {
            std::string which = name;
            return method(name.c_str(), [listObject, which](NativeCall& c) {
                Value function = c.argument(0);
                std::vector<Value> out;
                auto snapshot = listObject->items;
                for (const auto& item : snapshot) {
                    Value r = c.vm.callSync(function, {item});
                    if (which == "map") out.push_back(r);
                    else if (which == "filter") {
                        if (r.kind != ValueKind::boolean) throw ScriptError(tr("filter: la funzione deve restituire vero o falso"));
                        if (r.flag) out.push_back(item);
                    }
                }
                if (which == "forEach") return NativeResult::of(Value());
                return NativeResult::of(c.vm.list(std::move(out)));
            });
        }
        if (name == "sorted") return method("sorted", [listObject](NativeCall& c) {
            auto items = listObject->items;
            Value order = c.argument(0);
            std::string failure;
            std::stable_sort(items.begin(), items.end(), [&](const Value& a, const Value& b) {
                if (!failure.empty()) return false;
                try {
                    if (!order.isNone()) {
                        Value r = c.vm.callSync(order, {a, b});
                        return r.kind == ValueKind::boolean && r.flag;
                    }
                    if (a.numeric() && b.numeric()) return a.toNumber() < b.toNumber();
                    if (a.kind == ValueKind::text && b.kind == ValueKind::text) return c.vm.textOf(a) < c.vm.textOf(b);
                    failure = tr("sorted: senza funzione di ordine si possono ordinare solo numeri o testi");
                } catch (const ScriptError& e) {
                    failure = e.what();
                }
                return false;
            });
            if (!failure.empty()) throw ScriptError(failure);
            return NativeResult::of(c.vm.list(std::move(items)));
        });
        if (name == "reversed") return method("reversed", [listObject](NativeCall& c) {
            auto items = listObject->items;
            std::reverse(items.begin(), items.end());
            return NativeResult::of(c.vm.list(std::move(items)));
        });
        if (name == "copy") return method("copy", [listObject](NativeCall& c) { return NativeResult::of(c.vm.list(listObject->items)); });
        if (name == "join") return method("join", [listObject](NativeCall& c) {
            std::string separator = c.argument(0).isNone() ? std::string(", ") : textArgument(c.vm, c.argument(0), "join");
            std::string out;
            for (std::size_t i = 0; i < listObject->items.size(); ++i) {
                if (i) out += separator;
                out += c.vm.toText(listObject->items[i]);
            }
            return NativeResult::of(c.vm.text(out));
        });
        break;
    }
    case ValueKind::map: {
        auto mapObject = std::static_pointer_cast<MapObject>(object.object);
        if (name == "count") return Value::from(static_cast<long long>(mapObject->entries.size()));
        if (name == "isEmpty") return Value::boolean(mapObject->entries.empty());
        if (name == "keys" || name == "values") {
            std::vector<Value> out;
            for (const auto& [k, v] : mapObject->entries) out.push_back(name == "keys" ? k : v);
            return list(std::move(out));
        }
        auto method = [&](const char* label, NativeFunction f) { return boundNative(label, Value(), std::move(f)); };
        if (name == "contains") return method("contains", [mapObject](NativeCall& c) { return NativeResult::of(Value::boolean(mapObject->find(c.argument(0)) != nullptr)); });
        if (name == "remove") return method("remove", [mapObject](NativeCall& c) {
            const Value* found = mapObject->find(c.argument(0));
            Value removed = found ? *found : Value();
            mapObject->remove(c.argument(0));
            return NativeResult::of(removed);
        });
        if (name == "get") return method("get", [mapObject](NativeCall& c) {
            const Value* found = mapObject->find(c.parameter(0, "chiave"));
            return NativeResult::of(found ? *found : c.parameter(1, "altrimenti"));
        });
        if (name == "clear") return method("clear", [mapObject](NativeCall&) { mapObject->release(); return NativeResult::of(Value()); });
        if (name == "copy") return method("copy", [mapObject](NativeCall& c) {
            Value copy = c.vm.map();
            for (const auto& [k, v] : mapObject->entries) copy.as<MapObject>()->set(k, v);
            c.vm.charge(*copy.object, static_cast<long long>(mapObject->entries.size()) * 48);
            return NativeResult::of(copy);
        });
        break;
    }
    case ValueKind::data: {
        const auto* d = object.as<DataObject>();
        const auto& v = d->values;
        Value self = object;
        switch (d->kind) {
        case DataKind::vec3: {
            if (name == "x") return Value::real(v[0]);
            if (name == "y") return Value::real(v[1]);
            if (name == "z") return Value::real(v[2]);
            double length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (name == "length") return Value::real(length);
            if (name == "unit") return length > 1e-12 ? makeVec3(v[0] / length, v[1] / length, v[2] / length) : makeVec3(0, 0, 0);
            if (name == "dot") return boundNative("dot", Value(), [self](NativeCall& c) {
                const auto& a = self.as<DataObject>()->values;
                const auto& b = data(c.argument(0), DataKind::vec3, "dot").values;
                return NativeResult::of(Value::real(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
            });
            if (name == "cross") return boundNative("cross", Value(), [self](NativeCall& c) {
                const auto& a = self.as<DataObject>()->values;
                const auto& b = data(c.argument(0), DataKind::vec3, "cross").values;
                return NativeResult::of(makeVec3(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]));
            });
            if (name == "lerp") return boundNative("lerp", Value(), [self](NativeCall& c) {
                const auto& a = self.as<DataObject>()->values;
                const auto& b = data(c.argument(0), DataKind::vec3, "lerp").values;
                double t = number(c.argument(1), "lerp");
                return NativeResult::of(makeVec3(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t));
            });
            if (name == "distance") return boundNative("distance", Value(), [self](NativeCall& c) {
                const auto& a = self.as<DataObject>()->values;
                const auto& b = data(c.argument(0), DataKind::vec3, "distance").values;
                return NativeResult::of(Value::real(std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]))));
            });
            break;
        }
        case DataKind::vec2:
            if (name == "x") return Value::real(v[0]);
            if (name == "y") return Value::real(v[1]);
            if (name == "length") return Value::real(std::sqrt(v[0] * v[0] + v[1] * v[1]));
            break;
        case DataKind::color:
            if (name == "r") return Value::real(v[0]);
            if (name == "g") return Value::real(v[1]);
            if (name == "b") return Value::real(v[2]);
            if (name == "hex") return text(hexOf(v[0], v[1], v[2]));
            if (name == "lerp") return boundNative("lerp", Value(), [self](NativeCall& c) {
                const auto& a = self.as<DataObject>()->values;
                const auto& b = data(c.argument(0), DataKind::color, "lerp").values;
                double t = number(c.argument(1), "lerp");
                return NativeResult::of(makeColor(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t));
            });
            break;
        case DataKind::rotation:
            if (name == "x") return Value::real(v[0]);
            if (name == "y") return Value::real(v[1]);
            if (name == "z") return Value::real(v[2]);
            break;
        case DataKind::transform: {
            if (name == "position") return makeVec3(v[0], v[1], v[2]);
            if (name == "rotation") return makeData(DataKind::rotation, {v[3], v[4], v[5]});
            auto m = rotationMatrix(v[3], v[4], v[5]);
            if (name == "lookVector") return makeVec3(-m[0][2], -m[1][2], -m[2][2]);
            if (name == "rightVector") return makeVec3(m[0][0], m[1][0], m[2][0]);
            if (name == "upVector") return makeVec3(m[0][1], m[1][1], m[2][1]);
            if (name == "apply") return boundNative("apply", Value(), [self](NativeCall& c) {
                const auto& t = self.as<DataObject>()->values;
                const auto& p = data(c.argument(0), DataKind::vec3, "apply").values;
                auto r = rotationMatrix(t[3], t[4], t[5]);
                return NativeResult::of(makeVec3(t[0] + r[0][0] * p[0] + r[0][1] * p[1] + r[0][2] * p[2], t[1] + r[1][0] * p[0] + r[1][1] * p[1] + r[1][2] * p[2],
                                                 t[2] + r[2][0] * p[0] + r[2][1] * p[1] + r[2][2] * p[2]));
            });
            break;
        }
        case DataKind::ray:
            if (name == "origin") return makeVec3(v[0], v[1], v[2]);
            if (name == "direction") return makeVec3(v[3], v[4], v[5]);
            break;
        case DataKind::bounds:
            if (name == "low") return makeVec3(v[0], v[1], v[2]);
            if (name == "high") return makeVec3(v[3], v[4], v[5]);
            if (name == "center") return makeVec3((v[0] + v[3]) / 2, (v[1] + v[4]) / 2, (v[2] + v[5]) / 2);
            if (name == "size") return makeVec3(v[3] - v[0], v[4] - v[1], v[5] - v[2]);
            if (name == "contains") return boundNative("contains", Value(), [self](NativeCall& c) {
                const auto& b = self.as<DataObject>()->values;
                const auto& p = data(c.argument(0), DataKind::vec3, "contains").values;
                return NativeResult::of(Value::boolean(p[0] >= b[0] && p[1] >= b[1] && p[2] >= b[2] && p[0] <= b[3] && p[1] <= b[4] && p[2] <= b[5]));
            });
            break;
        case DataKind::dim:
            if (name == "scale") return Value::real(v[0]);
            if (name == "offset") return Value::real(v[1]);
            break;
        case DataKind::dim2:
            if (name == "xScale") return Value::real(v[0]);
            if (name == "xOffset") return Value::real(v[1]);
            if (name == "yScale") return Value::real(v[2]);
            if (name == "yOffset") return Value::real(v[3]);
            break;
        }
        break;
    }
    default: break;
    }
    throw ScriptError(tr("«{0}» non è un membro di {1}", name, kindName(object)));
}

void Vm::setMember(const Value& object, const std::string& name, const Value& value) {
    switch (object.kind) {
    case ValueKind::none: throw ScriptError(tr("Impossibile assegnare «{0}» su none: il valore non esiste", name));
    case ValueKind::host:
        if (!hostInterface) break;
        hostInterface->setMember(*this, object, name, value);
        return;
    case ValueKind::instance: {
        auto* instance = object.as<InstanceObject>();
        auto field = instance->type->fieldIndex.find(name);
        if (field == instance->type->fieldIndex.end()) throw ScriptError(tr("«{0}» non è un campo di {1}", name, instance->type->descriptor.name));
        bool writable = instance->type->descriptor.mutableFields[field->second];
        if (!writable && current && !current->frames.empty()) {
            const std::string& proto = current->frames.back().closure->proto().name;
            writable = proto == instance->type->descriptor.name + ".init" || proto == instance->type->descriptor.name + ".campi";
        }
        if (!writable) throw ScriptError(tr("Il campo «{0}» è una costante", name));
        instance->fields[field->second] = value;
        return;
    }
    default: break;
    }
    throw ScriptError(tr("«{0}» non si può assegnare su {1}", name, kindName(object)));
}

}
