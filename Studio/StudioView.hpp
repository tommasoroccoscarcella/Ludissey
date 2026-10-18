#pragma once

#include "Engine/Client/Store.hpp"
#include "Engine/Studio/Recovery.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Interface/Components.hpp"
#include "Interface/GuiOverlay.hpp"
#include "Interface/Surface.hpp"
#include "Studio/StudioStage.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace studio {

class StudioShell {
public:
    explicit StudioShell(std::shared_ptr<ld::Store> store);
    ~StudioShell();

    ld::StudioDocument document;
    std::shared_ptr<StudioStage> stage;
    std::function<void()> invalidated;
    std::function<void(const std::string&)> setTitle;
    std::function<void(bool)> setEdited;
    std::function<std::optional<std::filesystem::path>()> chooseMesh;
    std::function<std::optional<std::filesystem::path>(const std::vector<std::string>& extensions)> chooseContent;
    std::function<std::optional<std::filesystem::path>(const std::string& name, const std::string& extension)> chooseDestination;
    std::function<void(bool)> showSheet;

    ui::NodePtr build();
    ui::NodePtr insertSheet();
    void tick(double now, double delta);
    std::vector<ld::render::View> views(const std::vector<ui::ViewportSlot>& slots);
    bool pointer(const std::string& slot, const ui::PointerEvent& event);
    bool key(const ui::KeyInput& key);
    void resigned();
    void rendered(const ui::Surface& surface);
    std::function<void()> quit;
    bool requestQuit();
    void newProject();
    void showProjects();
    void requestClose();
    void save(bool publish);
    void saveAs();
    void openSettings();
    void showHelp();
    void togglePanel(const std::string& name);
    void resetLayout();
    void importModel();
    void frameSelection();
    void toggleView(const std::string& name);

private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<ld::Store> store;
    std::function<void()> leaving;
    std::function<void()> afterSave;
    bool helpOpen = false;
    bool publishing = false;
    double recoveredAt = -1;
    std::vector<ld::studio_recovery::Entry> recoveries;
    bool recoveriesLoaded = false;
    std::vector<ld::Experience> macProjects;
    std::optional<std::string> macOwner;
    ui::NodePtr macGroup();
    bool sheetShown = false;
    bool explorerActive = false;
    int stripe = 0;
    std::map<std::string, std::string> drafts;
    std::set<std::string> editing;
    std::optional<std::string> chosenClass;
    std::string command;
    std::string renameDraft;
    std::optional<std::string> renameFor;
    ui::FindAction finder = ui::FindAction::none;
    std::size_t outputCount = 0;
    ui::GuiOverlay overlay;

    void invalidate();
    void refresh();
    void openRequested();
    void open(const ld::Experience& project);
    void confirmLeaving(std::function<void()> proceed);
    void discardRecovery();
    void recover(const ld::studio_recovery::Entry& entry);
    ui::NodePtr helpSheet();

    ui::NodePtr projectBar();
    ui::NodePtr ribbon();
    ui::NodePtr ribbonRow(double width);
    ui::NodePtr ribbonLarge(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help = "");
    ui::NodePtr ribbonSmall(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help, bool labelled);
    ui::NodePtr ribbonGroup(const std::string& id, const std::string& title, std::vector<ui::NodePtr> items);
    std::string ribbonTab = "home";
    double insertAnchor = 0;
    ui::NodePtr pullDown(const std::string& id, ui::NodePtr face, bool enabled, std::function<std::vector<ui::MenuItem>()> items, const std::string& help, std::optional<ui::Color> ink = std::nullopt);
    ui::NodePtr toolWell(bool sunk, bool hovered);
    ui::NodePtr headerText(ui::NodePtr label);
    ui::NodePtr etched();
    ui::NodePtr viewButton(const std::string& id, const std::string& icon, const std::string& title, bool enabled, bool active, std::function<void()> action, const std::string& help = "");
    ui::NodePtr commandPicker(const std::string& id, const std::vector<std::string>& options, int selected, double width, std::function<void(int)> choose, const std::string& help);
    ui::NodePtr insertMenu();
    ui::NodePtr dock();
    ui::NodePtr terrainPanel();
    ui::NodePtr terrainCreateTab();
    ui::NodePtr terrainRegionTab();
    ui::NodePtr terrainBrushSettings();
    ui::NodePtr terrainGenerateSettings();
    ui::NodePtr terrainSeaSettings();
    ui::NodePtr terrainRegionFields();
    ui::NodePtr terrainField(const std::string& title, ui::NodePtr content);
    ui::NodePtr terrainMaterials(bool allowWater, ld::TerrainMaterial current, std::function<void(ld::TerrainMaterial)> pick);
    ui::NodePtr terrainForm();
    ui::NodePtr waterSwatches(const std::vector<std::string>& choices, const std::string& current, std::function<void(const std::string&)> pick);
    ui::NodePtr plainNumber(const std::string& id, double value, double low, double high, int digits, std::function<void(double)> change);
    std::string terrainHelp(ld::TerrainTool tool) const;
    bool confirmClearTerrain = false;
    ui::NodePtr utilityArea(double height, bool collapsed, const std::vector<std::string>& tabs);
    void loadLayout();
    void saveLayout(double now);
    std::string layoutSignature() const;
    bool insertOpen = false;
    std::string savedLayout;
    double layoutCheckedAt = 0;
    ui::NodePtr workspace(ui::Size size);
    ui::NodePtr divider(const std::string& id, bool vertical, std::function<void(double)> change);
    ui::NodePtr panelHeader(const std::string& title, const std::optional<std::string>& detail = std::nullopt, const std::string& panel = "");
    ui::NodePtr panelClose(const std::string& panel);
    int projectsTab = 0;
    ui::NodePtr explorer();
    ui::NodePtr serviceNode(const ld::ServiceDescriptor& service);
    ui::NodePtr node(const std::string& id, int indent);
    ui::NodePtr explorerRow(const std::string& id, const std::string& icon, const std::string& title, const std::optional<std::string>& detail, int indent, bool selected, bool expanded, bool hasChildren, bool locked,
                            std::function<void()> toggle);
    std::vector<ui::MenuItem> menu(const std::string& id);
    std::string iconFor(const std::string& id) const;
    std::string serviceTitle(const std::string& name) const;
    std::string serviceHelp(const std::string& name) const;
    std::string shownName(const std::string& id) const;
    ui::NodePtr center();
    ui::NodePtr panelTab(const std::string& id, ui::NodePtr label, bool active, std::function<void()> choose);
    ui::NodePtr centerTab(const std::string& id, const std::string& title, bool active, std::function<void()> close, std::function<void()> select);
    ui::NodePtr scriptPane(const std::string& id);
    ui::NodePtr properties();
    ui::NodePtr lightingForm();
    ui::NodePtr partForm(const ld::WorldBlock& block);
    ui::NodePtr instanceForm(const ld::InstanceRecord& record);
    ui::NodePtr propertyEditor(const ld::InstanceRecord& record, const std::string& name, const ld::PropertyDescriptor& info, const std::optional<ld::PropertyValue>& value);
    ui::NodePtr category(const std::string& title);
    ui::NodePtr row(const std::string& title, ui::NodePtr content, const std::string& help = "");
    ui::NodePtr readOnly(const std::string& value);
    ui::NodePtr propertyCell(ui::NodePtr name, ui::NodePtr content);
    ui::NodePtr toggleRow(const std::string& id, const std::string& title, bool value, std::function<void(bool)> change, const std::string& help = "");
    ui::NodePtr axis(const ld::WorldBlock& block, const std::string& title, double x, double y, double z, double low, double high, double step, std::function<void(int, double)> change);
    ui::NodePtr palette(const std::string& key, const std::vector<std::string>& colors, const std::string& current, std::function<void(const std::string&)> pick);
    ui::NodePtr numberField(const std::string& id, double value, double low, double high, double step, int digits, std::function<void(double)> change);
    ui::NodePtr vectorField(const std::string& id, const std::vector<double>& values, const std::vector<std::pair<double, double>>& ranges, std::function<void(int, double)> change);
    ui::NodePtr colorField(const std::string& id, const std::string& hex, std::function<void(const std::string&)> change);
    ui::NodePtr textField(const std::string& id, const std::string& text, std::function<void(const std::string&)> commit);
    ui::NodePtr classicField(const std::string& id, const std::string& prompt, const std::string& text, std::function<void(const std::string&)> change, bool enabled = true);
    ui::NodePtr toolbox();
    ui::NodePtr toolboxTile(const std::string& id, const std::string& icon, const std::string& title, bool enabled, std::function<void()> action, const std::string& help = "");
    ui::NodePtr output();
    std::pair<long long, long long> outputCounts() const;
    void showOutput();
    ui::NodePtr performance();
    ui::NodePtr apiBrowser();
    double performanceShown = 0;
    bool deferredRefresh = false;
    double deferredShown = 0;
    ui::NodePtr projects();
    ui::NodePtr imageSetting(ld::ExperienceImage kind);
    ui::NodePtr settings();
    ui::NodePtr searchField(const std::string& id, const std::string& prompt, const std::string& text, std::function<void(const std::string&)> change);
};

}
