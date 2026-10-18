#pragma once

#include "Engine/Client/Store.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Rendering/World.hpp"
#include "Interface/Surface.hpp"

#include <set>

namespace ui {

class ClientMedia {
public:
    explicit ClientMedia(std::shared_ptr<ld::Store> store) : store(std::move(store)) {}
    Image thumbnail(const std::string& id);
    Image icon(const std::string& id);
    Image experienceImage(const std::string& experience, const std::string& asset);
    Image itemImage(const ld::AvatarItem& item);
    Image portrait(const std::string& username);
    Image figure(const std::string& id);
    std::optional<ld::PortraitImages> portraitImages(const ld::AvatarConfiguration& look);
    std::vector<ld::render::View> views(const std::vector<ViewportSlot>& slots);
    void tick(double delta);
    void orbit(const std::string& slot, double dx, double dy);
    void zoom(const std::string& slot, double delta);
    void clear();

private:
    struct AvatarPreview {
        ld::render::Scene scene;
        ld::render::NodePtr camera;
        ld::render::CharacterPtr character;
        double yaw = std::atan2(3., 4.);
        double pitch = std::atan2(.9, 5.);
        double distance = 2.2;
        void positionCamera();
    };
    struct Thumbnail {
        std::string source;
        Image image;
    };
    std::shared_ptr<ld::Store> store;
    std::map<std::string, AvatarPreview> previews;
    std::map<std::string, Thumbnail> figures;
    std::map<std::string, Thumbnail> thumbnails;
    std::map<std::string, Image> itemImages;
    std::map<std::string, Image> uploaded;
    std::string portraitSource;
    Image portraitImage;
    std::shared_ptr<std::set<std::string>> fetching = std::make_shared<std::set<std::string>>();
};

}
