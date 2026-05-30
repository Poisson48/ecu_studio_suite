#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ecu {

struct Stage1 {
    std::map<std::string, int> pcts;
};

struct PopBang {
    int rpm;
    int fuelQty;
};

struct VehicleTemplate {
    std::string                      id;
    std::string                      name;
    std::string                      description;
    std::string                      vehicles;
    std::vector<std::string>         appliesTo;
    std::optional<std::vector<std::string>> appliesToVariant;
    std::optional<Stage1>            stage1;
    std::optional<PopBang>           popbang;
    std::vector<std::string>         autoMods;
};

struct TemplateSummary {
    std::string                      id;
    std::string                      name;
    std::string                      description;
    std::string                      vehicles;
    std::vector<std::string>         appliesTo;
    std::optional<std::vector<std::string>> appliesToVariant;
    bool                             hasStage1;
    bool                             hasPopbang;
    std::size_t                      autoModCount;
};

extern const std::vector<VehicleTemplate> VEHICLE_TEMPLATES;

std::vector<TemplateSummary>   listTemplates();
std::optional<VehicleTemplate> getTemplate(const std::string& id);
std::vector<TemplateSummary>   listTemplatesForEcu(const std::string& ecuId);
std::vector<TemplateSummary>   listTemplatesForVariant(const std::string& ecuId,
                                                        const std::string& variant);

} // namespace ecu
