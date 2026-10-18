#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lim {

inline constexpr std::uint16_t bytecodeVersion = 4;
inline constexpr std::string_view languageVersion = "0.9";

enum class Op : std::uint8_t {
    nop,
    constant,
    none,
    pushTrue,
    pushFalse,
    pop,
    dup,
    dup2,
    getLocal,
    setLocal,
    makeCell,
    getCell,
    setCell,
    getUpvalue,
    setUpvalue,
    getGlobal,
    setGlobal,
    defineGlobal,
    getMember,
    getMemberOptional,
    setMember,
    getIndex,
    getIndexOptional,
    setIndex,
    call,
    callNamed,
    add,
    subtract,
    multiply,
    divide,
    modulo,
    equal,
    notEqual,
    less,
    lessEqual,
    greater,
    greaterEqual,
    negate,
    logicalNot,
    jump,
    jumpIfFalse,
    jumpIfTrue,
    jumpIfFalseKeep,
    jumpIfTrueKeep,
    jumpIfNotNoneKeep,
    jumpIfNoneKeep,
    makeList,
    makeMap,
    makeRange,
    iterStart,
    iterNext,
    interpolate,
    closure,
    returnValue,
    returnNone,
    await,
    connect,
    import,
    makeClass,
    makeEnum,
    isType,
    shorthand,
    cast,
    count
};

std::string_view opName(Op op);

struct EnumConstant {
    std::string type;
    std::string name;
    bool operator==(const EnumConstant&) const = default;
};

struct Constant {
    enum class Kind { integer, number, text, enumValue, names } kind = Kind::integer;
    long long integer = 0;
    double number = 0;
    std::string text;
    EnumConstant enumValue;
    std::vector<std::string> names;
    bool operator==(const Constant&) const = default;
};

struct Instruction {
    Op op = Op::nop;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    bool operator==(const Instruction&) const = default;
};

struct LocalName {
    std::uint32_t slot = 0;
    std::uint32_t from = 0;
    std::string name;
    bool operator==(const LocalName&) const = default;
};

struct Capture {
    bool fromLocal = true;
    std::uint32_t index = 0;
    bool operator==(const Capture&) const = default;
};

struct ClassDescriptor {
    std::string name;
    std::vector<std::string> fields;
    std::vector<bool> mutableFields;
    std::uint32_t fieldInitializer = 0;
    bool hasFieldInitializer = false;
    std::vector<std::pair<std::string, std::uint32_t>> methods;
    bool operator==(const ClassDescriptor&) const = default;
};

struct EnumDescriptor {
    std::string name;
    std::vector<std::string> cases;
    bool operator==(const EnumDescriptor&) const = default;
};

struct Prototype {
    std::string name;
    std::uint32_t parameters = 0;
    std::uint32_t minimum = 0;
    std::uint32_t locals = 0;
    std::uint32_t maxStack = 0;
    bool async = false;
    bool method = false;
    std::vector<std::string> parameterNames;
    std::vector<std::uint32_t> defaults;
    std::vector<Instruction> code;
    std::vector<std::uint32_t> lines;
    std::vector<std::uint32_t> columns;
    std::vector<Constant> constants;
    std::vector<Capture> captures;
    std::vector<ClassDescriptor> classes;
    std::vector<EnumDescriptor> enums;
    std::vector<bool> cellParameters;
    std::vector<LocalName> localNames;
    bool operator==(const Prototype&) const = default;
};

struct Module {
    std::string name;
    std::string file;
    std::string context;
    std::vector<Prototype> prototypes;
    std::uint32_t main = 0;
    std::vector<std::string> exports;
    bool operator==(const Module&) const = default;
};

std::string serialize(const Module& module);
struct LoadResult {
    std::optional<Module> module;
    std::string error;
};
LoadResult deserialize(std::string_view data);
std::optional<std::string> validate(const Module& module);
std::uint32_t measureStack(const Prototype& prototype);

}
