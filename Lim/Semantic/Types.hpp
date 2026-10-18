#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lim {

struct Type;
using TypePtr = std::shared_ptr<const Type>;

enum class ExecutionContext { shared, server, client, edit };

std::string_view contextName(ExecutionContext context);

struct Type {
    enum class Kind { any, none, boolean, integer, number, text, list, map, optional, function, user, enumeration, host, module, task, error };
    Kind kind = Kind::any;
    std::string name;
    std::vector<TypePtr> arguments;
    TypePtr result;
    bool async = false;
    std::vector<std::string> parameterNames;
    int minimumArguments = -1;

    static TypePtr any();
    static TypePtr none();
    static TypePtr boolean();
    static TypePtr integer();
    static TypePtr number();
    static TypePtr text();
    static TypePtr error();
    static TypePtr list(TypePtr element);
    static TypePtr map(TypePtr key, TypePtr value);
    static TypePtr optional(TypePtr inner);
    static TypePtr function(std::vector<TypePtr> parameters, TypePtr result, bool async = false, int minimum = -1, std::vector<std::string> names = {});
    static TypePtr user(std::string name);
    static TypePtr enumeration(std::string name);
    static TypePtr host(std::string name);
    static TypePtr module(std::string name);
    static TypePtr task(TypePtr result);

    std::string describe() const;
    bool numeric() const { return kind == Kind::integer || kind == Kind::number; }
    bool isAny() const { return kind == Kind::any || kind == Kind::error; }
};

TypePtr unwrapOptional(const TypePtr& type);
bool sameType(const TypePtr& a, const TypePtr& b);

struct HostMember {
    std::string name;
    TypePtr type;
    bool readOnly = false;
    std::string doc;
    ExecutionContext only = ExecutionContext::shared;
    bool deprecated = false;
    std::string replacement;
};

struct HostMethod {
    std::string name;
    std::vector<std::string> parameterNames;
    std::vector<TypePtr> parameters;
    int minimum = 0;
    bool variadic = false;
    TypePtr result;
    bool async = false;
    std::string doc;
    ExecutionContext only = ExecutionContext::shared;
    bool deprecated = false;
    std::string replacement;
};

struct HostEvent {
    std::string name;
    std::vector<std::string> parameterNames;
    std::vector<TypePtr> parameters;
    std::string doc;
    bool variadic = false;
    bool deprecated = false;
    std::string replacement;
};

enum class MemberKind { none, property, method, event };

struct HostClass {
    std::string name;
    std::string base;
    bool creatable = false;
    bool value = false;
    std::string doc;
    std::map<std::string, HostMember> properties;
    std::map<std::string, HostMethod> methods;
    std::map<std::string, HostEvent> events;
    std::optional<HostMethod> constructor;
    std::map<std::string, HostMethod> statics;
    std::map<std::string, HostMember> constants;
};

struct HostGlobal {
    std::string name;
    TypePtr type;
    std::string doc;
    ExecutionContext only = ExecutionContext::shared;
};

class TypeRegistry {
public:
    void addClass(HostClass definition);
    void addGlobal(HostGlobal global);
    void addFunction(HostMethod function);
    void addEnum(std::string name, std::vector<std::string> items);

    const HostClass* findClass(std::string_view name) const;
    const HostGlobal* findGlobal(std::string_view name) const;
    const HostMethod* findFunction(std::string_view name) const;
    const std::vector<std::string>* findEnum(std::string_view name) const;
    bool isA(std::string_view name, std::string_view ancestor) const;
    const HostMember* property(std::string_view className, std::string_view name) const;
    const HostMethod* method(std::string_view className, std::string_view name) const;
    const HostEvent* event(std::string_view className, std::string_view name) const;
    std::vector<const HostMember*> properties(std::string_view className) const;
    std::vector<const HostMethod*> methods(std::string_view className) const;
    std::vector<const HostEvent*> events(std::string_view className) const;
    MemberKind subclassMember(std::string_view className, std::string_view name) const;

    const std::map<std::string, HostClass>& classes() const { return classTable; }
    const std::map<std::string, HostGlobal>& globals() const { return globalTable; }
    const std::map<std::string, HostMethod>& functions() const { return functionTable; }
    const std::map<std::string, std::vector<std::string>>& enums() const { return enumTable; }

private:
    std::map<std::string, HostClass> classTable;
    std::map<std::string, HostGlobal> globalTable;
    std::map<std::string, HostMethod> functionTable;
    std::map<std::string, std::vector<std::string>> enumTable;
};

TypeRegistry standardRegistry();
void addStandardLibrary(TypeRegistry& registry);

}
