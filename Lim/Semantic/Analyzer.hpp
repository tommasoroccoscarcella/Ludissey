#pragma once

#include "Lim/AST/Ast.hpp"
#include "Lim/Diagnostics/Diagnostic.hpp"
#include "Types.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lim {

enum class SymbolKind { variable, constant, parameter, function, classType, enumType, enumCase, field, method, module, global, builtin };

struct Symbol {
    int id = 0;
    std::string name;
    SymbolKind kind = SymbolKind::variable;
    TypePtr type;
    Range range;
    std::string container;
    std::string doc;
    bool mutable_ = false;
    bool external = false;
    bool inferred = false;
};

struct Reference {
    Range range;
    int symbol = -1;
};

struct ScopeInfo {
    Range range;
    int parent = -1;
    std::vector<int> symbols;
};

struct ModuleInterface {
    std::string name;
    std::map<std::string, TypePtr> exports;
    std::map<std::string, std::vector<std::pair<std::string, TypePtr>>> classFields;
    std::map<std::string, std::vector<std::string>> enums;
};

struct UserClass {
    std::string name;
    std::vector<std::pair<std::string, TypePtr>> fields;
    std::map<std::string, bool> mutableFields;
    std::map<std::string, TypePtr> methods;
    std::optional<TypePtr> initializer;
    Range range;
};

struct Analysis {
    std::vector<Diagnostic> diagnostics;
    std::vector<Symbol> symbols;
    std::vector<Reference> references;
    std::vector<ScopeInfo> scopes;
    std::map<const Expr*, TypePtr> types;
    std::map<const Expr*, int> resolved;
    ModuleInterface interface;
    std::map<std::string, UserClass> classes;
    std::map<std::string, std::vector<std::string>> enums;
    std::vector<std::string> imports;
    bool usesAwait = false;
};

struct AnalyzeOptions {
    ExecutionContext context = ExecutionContext::shared;
    std::string file = "script";
    std::function<std::optional<ModuleInterface>(const std::string&)> resolveModule;
    bool topLevelAsync = true;
    std::vector<std::pair<std::string, TypePtr>> extraGlobals;
};

Analysis analyze(const Program& program, const TypeRegistry& registry, const AnalyzeOptions& options);
bool assignable(const TypePtr& from, const TypePtr& to, const TypeRegistry& registry);
TypePtr classReference(const std::string& name);
bool isClassReference(const TypePtr& type);
std::string classReferenceName(const TypePtr& type);
TypePtr builtinMember(const TypePtr& owner, const std::string& name);
std::vector<std::string> builtinMembers(const TypePtr& owner);

}
