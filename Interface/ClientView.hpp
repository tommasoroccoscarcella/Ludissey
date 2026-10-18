#pragma once

#include "Engine/Client/Store.hpp"
#include "Interface/Components.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ui {

using ld::Shelf;

class ClientView {
public:
    explicit ClientView(std::shared_ptr<ld::Store> store);
    NodePtr build();
    std::function<void()> invalidated;
    std::function<void(std::optional<int>)> graphicsChanged;
    std::function<Image(const std::string&)> thumbnail;
    std::function<Image(const std::string&)> experienceIcon;
    std::function<Image(const ld::AvatarItem&)> itemImage;
    std::function<Image(const std::string&)> portrait;
    std::function<Image(const std::string&)> figure;
    std::function<NodePtr()> playerShell;
    std::function<NodePtr()> studioShell;
    std::function<void(const std::string&)> focusField;

private:
    std::shared_ptr<ld::Store> store;
    bool recovering = false;
    std::string recoveryEmail;
    std::string loginName, loginPassword;
    std::string username, password, confirmation, gender = "male";
    std::string errorField;
    std::string friendError;
    std::string friendName, email, bio;
    std::optional<std::string> genre;
    int avatarTab = 0;
    int friendsTab = 0;
    int profileTab = 0;
    NodePtr profilePage();
    NodePtr profileActions(const ld::PlayerPage& page);
    void openOwnProfile();
    void openPlayer(const std::string& id, const std::string& username);
    int settingsTab = 0;
    bool bioLoaded = false;
    bool authenticated = false;
    bool accountMenu = false;
    bool searchOpen = false;
    std::string requestedDetails;

    void invalidate();
    void fail(const std::string& field, const std::string& message);
    void leave(std::function<void()> action);
    NodePtr login();
    NodePtr loginBar(double width);
    NodePtr loginPanel();
    NodePtr characterChoice();
    NodePtr fieldError(const std::string& field);
    NodePtr client();
    NodePtr navigation(double width);
    NodePtr navigationItem(const std::string& id, const std::string& title, bool selected, int count, std::function<void()> action);
    NodePtr utility(const std::string& id, const std::string& icon, int count, const std::string& label, bool open, std::function<void()> action);
    NodePtr searchField(double width);
    std::string searchPrompt() const;
    NodePtr bust(const std::string& name, double side);
    NodePtr facts(const ld::ExperienceEntry& entry, const ld::ExperienceDetails* details);
    NodePtr page();
    std::string backTitle() const;
    NodePtr home();
    NodePtr experiences();
    NodePtr experience(const ld::ExperienceEntry& entry);
    NodePtr servers(const ld::ExperienceEntry& entry, const ld::ExperienceDetails* details);
    NodePtr friends();
    NodePtr settings();
    NodePtr avatar();
    NodePtr avatarEditor(const ld::AvatarConfiguration& shown);
    NodePtr notifications();
    NodePtr accountDropdown();
    NodePtr joinOverlay();
    NodePtr cover(const ld::ExperienceEntry& entry, double width, double height);
    NodePtr experienceCard(const ld::ExperienceEntry& entry, double side);
    NodePtr shelf(Shelf kind, const std::string& title, double width);
    NodePtr experienceList(const std::vector<ld::ExperienceEntry>& entries, double width);
    NodePtr experienceRow(const ld::ExperienceEntry& entry);
    Image badge(const ld::ExperienceEntry& entry);
    int playing(const ld::ExperienceEntry& entry) const;
    double pointer(const std::string& anchor, double width) const;
    std::vector<ld::ExperienceEntry> shelfEntries(Shelf kind) const;
    NodePtr avatarTile(const ld::AvatarItem& item, bool worn, bool offered);
    std::vector<Image> loginBackdrops;
    NodePtr field(const std::string& id, const std::string& prompt, std::string& value, bool secure = false, std::function<void()> commit = {}, bool bare = false, bool multiline = false);
    ld::AvatarConfiguration shownAvatar() const;
};

}
