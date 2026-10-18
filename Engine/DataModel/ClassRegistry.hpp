#pragma once

#include "Engine/Core/Result.hpp"
#include "Engine/Experience/Experience.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

struct PropertyDescriptor {
    std::string name;
    std::string category;
    std::string typeName;
    bool readOnly = false;
    bool deprecated = false;
    std::string owner;
    bool operator==(const PropertyDescriptor&) const = default;
};

struct ClassDescriptor {
    std::string className;
    std::optional<std::string> superclass;
    std::vector<std::string> tags;
    std::string category;
    int explorerOrder = 999;
    std::string preferredParent;
    bool deprecated = false;
    bool metadataInsertable = true;
    std::map<std::string, PropertyDescriptor> properties;
    std::set<std::string> functions;
    std::set<std::string> events;
    std::map<std::string, std::vector<std::string>> eventParameters;
    bool implemented = false;
    std::optional<std::string> runtime;
    std::string icon = "ld-icon-object-part";
    std::vector<std::string> parents;
    std::vector<std::string> supported;
    std::optional<std::string> kind;
    bool insertFlag = true;
    PropertyMap defaults;

    bool creatable() const;
    bool canCreate() const { return implemented && creatable() && !deprecated; }
    bool insertable() const { return canCreate() && insertFlag && metadataInsertable; }
    bool hasTag(std::string_view tag) const;
};

struct ServiceDescriptor {
    std::string name;
    std::string icon;
};

class ClassRegistry {
public:
    static Result<std::shared_ptr<ClassRegistry>> load(const std::filesystem::path& apiFile, const std::filesystem::path& rulesFile);
    static void setShared(std::shared_ptr<ClassRegistry> registry);
    static const ClassRegistry& shared();
    static bool hasShared();

    const ClassDescriptor* find(std::string_view className) const;
    const std::map<std::string, ClassDescriptor>& descriptors() const { return classes; }
    const std::vector<ServiceDescriptor>& services() const { return serviceList; }
    const std::map<std::string, std::vector<std::string>>& serviceChildren() const { return childrenOfServices; }
    const std::vector<std::pair<std::string, std::string>>& defaultChildren() const { return defaultInstances; }
    const std::map<std::string, std::vector<std::string>>& enums() const { return enumeration; }
    const std::map<std::string, std::vector<std::string>>& groups() const { return groupTable; }
    int dataset() const { return datasetYear; }

    bool isA(std::string_view className, std::string_view ancestor) const;
    bool isService(std::string_view name) const;
    bool canParent(std::string_view child, std::string_view parent) const;
    std::vector<const ClassDescriptor*> insertable(std::string_view parent) const;
    PropertyMap defaults(std::string_view className) const;

private:
    std::map<std::string, ClassDescriptor> classes;
    std::vector<ServiceDescriptor> serviceList;
    std::map<std::string, std::vector<std::string>> childrenOfServices;
    std::vector<std::pair<std::string, std::string>> defaultInstances;
    std::map<std::string, std::vector<std::string>> enumeration;
    std::map<std::string, std::vector<std::string>> groupTable;
    int datasetYear = 0;
};

}
