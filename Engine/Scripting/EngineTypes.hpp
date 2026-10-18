#pragma once

#include "Engine/DataModel/ClassRegistry.hpp"
#include "Lim/Semantic/Types.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

enum class ScriptContext { server, client, edit };

lim::ExecutionContext limContext(ScriptContext context);
std::string_view contextTitle(ScriptContext context);

std::string limMemberName(std::string_view registryName, std::string_view typeName = {});

struct MemberNames {
    std::map<std::string, std::string> properties;
    std::map<std::string, std::string> methods;
    std::map<std::string, std::string> events;
    std::map<std::string, std::string> propertyLim;
};

class EngineTypes {
public:
    static const EngineTypes& shared();
    explicit EngineTypes(const ClassRegistry& registry);

    static const std::set<std::string>& emittedEvents();
    const lim::TypeRegistry& registry() const { return types; }
    const MemberNames* members(std::string_view className) const;
    std::optional<std::string> property(std::string_view className, std::string_view limName) const;
    std::optional<std::string> method(std::string_view className, std::string_view limName) const;
    std::optional<std::string> event(std::string_view className, std::string_view limName) const;
    std::string limProperty(std::string_view className, std::string_view registryName) const;
    std::vector<std::pair<std::string, std::string>> serviceGlobals(ScriptContext context) const;

private:
    const ClassRegistry& classes;
    lim::TypeRegistry types;
    std::map<std::string, MemberNames> names;
    void addClass(const ClassDescriptor& descriptor);
    void addEngineLibrary();
    void addCreatorMembers(const ClassDescriptor& descriptor, lim::HostClass& c, MemberNames& own);
};

lim::TypePtr limType(const PropertyDescriptor& property);

}
