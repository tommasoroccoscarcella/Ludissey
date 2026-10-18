#pragma once

#include <array>
#include <string_view>

namespace ld::rules {

namespace site {
    inline constexpr std::string_view host = std::string_view("ludissey.com");
    inline constexpr std::string_view url = std::string_view("https://ludissey.com");
}
inline constexpr std::array<std::string_view, 7> genres{std::string_view("Avventura"), std::string_view("Ruolo"), std::string_view("Simulazione"), std::string_view("Piattaforme"), std::string_view("Combattimento"), std::string_view("Corse"), std::string_view("Altro")};
inline constexpr std::string_view defaultGenre = std::string_view("Altro");
namespace serverSize {
    inline constexpr int minimum = 1;
    inline constexpr int maximum = 50;
    inline constexpr int standard = 20;
}
inline constexpr int playerProtocol = 1;
inline constexpr int schemaVersion = 6;
inline constexpr int reservationSeconds = 120;
inline constexpr int heartbeatSeconds = 20;
inline constexpr int playerTimeoutSeconds = 75;
inline constexpr int emptyServerSeconds = 60;
inline constexpr int transactionAttempts = 6;
inline constexpr int statePollSeconds = 2;
inline constexpr int chatMessages = 30;
inline constexpr int chatCharacters = 120;
inline constexpr int chatBurst = 5;
inline constexpr int chatWindowSeconds = 10;
inline constexpr int respawnSeconds = 3;
inline constexpr int maximumHealth = 100;
namespace world {
    inline constexpr int parts = 200;
    inline constexpr int horizontal = 25;
    inline constexpr int vertical = 25;
    inline constexpr int voidHeight = -40;
    inline constexpr double minimumSize = 0.05;
    inline constexpr int maximumSize = 200;
    inline constexpr int payloadCharacters = 9500;
    inline constexpr int titleCharacters = 60;
    inline constexpr int nameCharacters = 40;
    inline constexpr int experiencesPerDeveloper = 20;
}
namespace part {
    inline constexpr std::string_view color = std::string_view("9AA0A8");
    inline constexpr std::array<std::string_view, 24> palette{std::string_view("D8473B"), std::string_view("A82D24"), std::string_view("F08A80"), std::string_view("2E6FF2"), std::string_view("1F52C4"), std::string_view("8FB4F7"), std::string_view("F2C230"), std::string_view("D9A400"), std::string_view("2FA84F"), std::string_view("1E7537"), std::string_view("8FD19E"), std::string_view("F28C28"), std::string_view("C75B12"), std::string_view("FFFFFF"), std::string_view("C9CDD3"), std::string_view("9AA0A8"), std::string_view("5B6169"), std::string_view("1B1D21"), std::string_view("8B5A2B"), std::string_view("5C3A1E"), std::string_view("F2A6C1"), std::string_view("7B4BC9"), std::string_view("4FC3E8"), std::string_view("1F8FB3")};
}
namespace baseplate {
    inline constexpr std::string_view name = std::string_view("Baseplate");
    inline constexpr int width = 60;
    inline constexpr int height = 1;
    inline constexpr bool locked = false;
}
inline constexpr std::array<std::string_view, 5> materials{std::string_view("plastic"), std::string_view("wood"), std::string_view("metal"), std::string_view("neon"), std::string_view("glass")};
namespace lighting {
    namespace clockTime {
        inline constexpr int minimum = 0;
        inline constexpr int maximum = 24;
        inline constexpr int standard = 14;
    }
    namespace brightness {
        inline constexpr int minimum = 0;
        inline constexpr int maximum = 3;
        inline constexpr int standard = 1;
    }
    namespace fogEnd {
        inline constexpr int minimum = 20;
        inline constexpr int maximum = 1000;
        inline constexpr int standard = 400;
    }
    inline constexpr std::string_view ambient = std::string_view("8A8FA0");
    inline constexpr std::string_view skyTop = std::string_view("5B8ED6");
    inline constexpr std::string_view skyHorizon = std::string_view("CFE3F7");
}
inline constexpr std::array<std::string_view, 4> containers{std::string_view("Workspace"), std::string_view("ReplicatedStorage"), std::string_view("ServerStorage"), std::string_view("StarterPack")};
inline constexpr std::array<std::string_view, 8> kinds{std::string_view("cube"), std::string_view("sphere"), std::string_view("cylinder"), std::string_view("wedge"), std::string_view("mesh"), std::string_view("model"), std::string_view("spawn"), std::string_view("seat")};
namespace swimming {
    inline constexpr double speedFactor = 0.55;
    inline constexpr double buoyancy = 0.85;
    inline constexpr int drag = 3;
    inline constexpr int ascend = 5;
}
namespace terrain {
    inline constexpr int cellSize = 2;
    inline constexpr int cellsX = 32;
    inline constexpr int cellsY = 16;
    inline constexpr int cellsZ = 32;
    inline constexpr int chunk = 8;
    inline constexpr int bottom = -16;
    inline constexpr int levels = 15;
    inline constexpr std::array<std::string_view, 6> materials{std::string_view("air"), std::string_view("grass"), std::string_view("rock"), std::string_view("sand"), std::string_view("mud"), std::string_view("water")};
    inline constexpr std::array<std::string_view, 6> colors{std::string_view("000000"), std::string_view("5E9B3C"), std::string_view("7F8087"), std::string_view("D8C68E"), std::string_view("6B4E36"), std::string_view("3E7FD6")};
    inline constexpr int keys = 4;
    inline constexpr int keyCharacters = 9000;
    namespace brush {
        inline constexpr int minimum = 1;
        inline constexpr int maximum = 12;
        inline constexpr int standard = 4;
    }
    namespace generation {
        inline constexpr int amplitude = 6;
        inline constexpr int baseHeight = 2;
        inline constexpr int waterLevel = 0;
        inline constexpr int octaves = 3;
    }
    inline constexpr double stepUp = 0.75;
    inline constexpr int format = 2;
    namespace water {
        inline constexpr std::string_view color = std::string_view("3E7FD6");
        inline constexpr int seaRing = 1000;
        inline constexpr std::array<std::string_view, 8> palette{std::string_view("3E7FD6"), std::string_view("2B5FA8"), std::string_view("1F8A9E"), std::string_view("3BA7A0"), std::string_view("6FB7D9"), std::string_view("2F4F6F"), std::string_view("5E7F3A"), std::string_view("8B6B45")};
        namespace transparency {
            inline constexpr double standard = 0.35;
            inline constexpr int minimum = 0;
            inline constexpr int maximum = 1;
        }
        namespace reflectance {
            inline constexpr double standard = 0.6;
            inline constexpr int minimum = 0;
            inline constexpr int maximum = 1;
        }
        namespace waveSize {
            inline constexpr double standard = 0.12;
            inline constexpr int minimum = 0;
            inline constexpr int maximum = 1;
        }
        namespace waveSpeed {
            inline constexpr int standard = 8;
            inline constexpr int minimum = 0;
            inline constexpr int maximum = 100;
        }
    }
}
namespace replication {
    inline constexpr int intervalMilliseconds = 500;
    inline constexpr int delayMilliseconds = 1000;
    inline constexpr int snapshots = 12;
    inline constexpr int staleSeconds = 6;
}
inline constexpr int registryCharacters = 30000;
namespace lim {
    inline constexpr std::string_view languageVersion = std::string_view("0.9");
    inline constexpr std::array<std::string_view, 1> languageVersions{std::string_view("0.9")};
    inline constexpr int engineApiVersion = 1;
    inline constexpr int schemaVersion = 5;
}
namespace scriptData {
    inline constexpr int characters = 9000;
    inline constexpr int experiences = 40;
}
namespace serverList {
    inline constexpr std::string_view emptyTitle = std::string_view("Nessun server attivo");
    inline constexpr std::string_view emptyDetail = std::string_view("Premi Gioca per avviare il primo server.");
}
inline constexpr int maxServers = 40;
namespace locomotion {
    inline constexpr double stepHeight = 0.35;
    inline constexpr int maxSlope = 60;
    inline constexpr int substepHertz = 120;
    inline constexpr double snapDistance = 0.05;
    inline constexpr int simulationHertz = 60;
    inline constexpr int seatCooldownSeconds = 2;
}
namespace avatar {
    inline constexpr int nameCharacters = 20;
    inline constexpr std::array<std::string_view, 2> bodyPresets{std::string_view("male"), std::string_view("female")};
    inline constexpr std::array<std::string_view, 2> bodyPresetTitles{std::string_view("Maschile"), std::string_view("Femminile")};
    inline constexpr std::array<std::string_view, 6> bodyParts{std::string_view("head"), std::string_view("torso"), std::string_view("leftArm"), std::string_view("rightArm"), std::string_view("leftLeg"), std::string_view("rightLeg")};
    inline constexpr std::array<std::string_view, 6> bodyPartTitles{std::string_view("Testa"), std::string_view("Busto"), std::string_view("Braccio sinistro"), std::string_view("Braccio destro"), std::string_view("Gamba sinistra"), std::string_view("Gamba destra")};
    inline constexpr std::array<std::string_view, 24> palette{std::string_view("F1C9A5"), std::string_view("E0AC86"), std::string_view("C68A5E"), std::string_view("A0694A"), std::string_view("6E4630"), std::string_view("FFE0C4"), std::string_view("6633EE"), std::string_view("268FBD"), std::string_view("EC7855"), std::string_view("2FA84F"), std::string_view("E0392B"), std::string_view("F2C230"), std::string_view("1B1D21"), std::string_view("F2F3F5"), std::string_view("9AA0A8"), std::string_view("3E4A61"), std::string_view("A3A2A5"), std::string_view("F5CD30"), std::string_view("0D69AC"), std::string_view("4B974B"), std::string_view("C4281C"), std::string_view("DA8541"), std::string_view("1B2A35"), std::string_view("F2F3F3")};
    inline constexpr std::array<std::string_view, 25> categories{std::string_view("hat"), std::string_view("hair"), std::string_view("faceAccessory"), std::string_view("neckAccessory"), std::string_view("shoulderAccessory"), std::string_view("frontAccessory"), std::string_view("backAccessory"), std::string_view("waistAccessory"), std::string_view("shirt"), std::string_view("tshirt"), std::string_view("pants"), std::string_view("gear"), std::string_view("face"), std::string_view("head"), std::string_view("torso"), std::string_view("rightArm"), std::string_view("leftArm"), std::string_view("rightLeg"), std::string_view("leftLeg"), std::string_view("idleAnimation"), std::string_view("walkAnimation"), std::string_view("runAnimation"), std::string_view("jumpAnimation"), std::string_view("climbAnimation"), std::string_view("fallAnimation")};
    inline constexpr std::array<std::string_view, 25> categoryTitles{std::string_view("Cappelli"), std::string_view("Acconciature"), std::string_view("Accessori viso"), std::string_view("Accessori collo"), std::string_view("Accessori spalle"), std::string_view("Accessori frontali"), std::string_view("Accessori schiena"), std::string_view("Accessori in vita"), std::string_view("Maglie"), std::string_view("T-shirt"), std::string_view("Pantaloni"), std::string_view("Equipaggiamento"), std::string_view("Volti"), std::string_view("Teste"), std::string_view("Busti"), std::string_view("Braccia destre"), std::string_view("Braccia sinistre"), std::string_view("Gambe destre"), std::string_view("Gambe sinistre"), std::string_view("Riposo"), std::string_view("Camminata"), std::string_view("Corsa"), std::string_view("Salto"), std::string_view("Arrampicata"), std::string_view("Caduta")};
    inline constexpr std::array<std::string_view, 8> accessories{std::string_view("hat"), std::string_view("hair"), std::string_view("faceAccessory"), std::string_view("neckAccessory"), std::string_view("shoulderAccessory"), std::string_view("frontAccessory"), std::string_view("backAccessory"), std::string_view("waistAccessory")};
    namespace groups {
        inline constexpr std::array<std::string_view, 12> clothing{std::string_view("hat"), std::string_view("hair"), std::string_view("faceAccessory"), std::string_view("neckAccessory"), std::string_view("shoulderAccessory"), std::string_view("frontAccessory"), std::string_view("backAccessory"), std::string_view("waistAccessory"), std::string_view("shirt"), std::string_view("tshirt"), std::string_view("pants"), std::string_view("gear")};
        inline constexpr std::array<std::string_view, 7> body{std::string_view("face"), std::string_view("head"), std::string_view("torso"), std::string_view("rightArm"), std::string_view("leftArm"), std::string_view("rightLeg"), std::string_view("leftLeg")};
        inline constexpr std::array<std::string_view, 6> animations{std::string_view("idleAnimation"), std::string_view("walkAnimation"), std::string_view("runAnimation"), std::string_view("jumpAnimation"), std::string_view("climbAnimation"), std::string_view("fallAnimation")};
    }
    namespace limits {
        inline constexpr int hats = 3;
        inline constexpr int accessories = 10;
    }
    namespace standard {
        inline constexpr std::string_view bodyPreset = std::string_view("male");
        namespace bodyColors {
            inline constexpr std::string_view head = std::string_view("F1C9A5");
            inline constexpr std::string_view torso = std::string_view("6633EE");
            inline constexpr std::string_view leftArm = std::string_view("F1C9A5");
            inline constexpr std::string_view rightArm = std::string_view("F1C9A5");
            inline constexpr std::string_view leftLeg = std::string_view("3E4A61");
            inline constexpr std::string_view rightLeg = std::string_view("3E4A61");
        }
    }
    namespace view {
        inline constexpr double pitchMinimum = -0.05;
        inline constexpr double pitchMaximum = 1.2;
        inline constexpr double distanceMinimum = 1.5;
        inline constexpr int distanceMaximum = 6;
    }
}
namespace instances {
    inline constexpr int maximum = 400;
    inline constexpr int keys = 6;
    inline constexpr int keyCharacters = 9000;
    inline constexpr int sourceCharacters = 20000;
    inline constexpr int nameCharacters = 60;
    inline constexpr int textCharacters = 400;
}
namespace account {
    inline constexpr std::string_view titleId = std::string_view("1948A2");
    inline constexpr int bioCharacters = 140;
    inline constexpr int portraitPlayers = 60;
    inline constexpr int portraitBust = 150;
    inline constexpr int portraitFigure = 352;
    inline constexpr int presenceSeconds = 90;
}
namespace serverStates {
    inline constexpr std::string_view provisioning = std::string_view("provisioning");
    inline constexpr std::string_view ready = std::string_view("ready");
    inline constexpr std::string_view joinable = std::string_view("joinable");
    inline constexpr std::string_view full = std::string_view("full");
    inline constexpr std::string_view draining = std::string_view("draining");
    inline constexpr std::string_view terminated = std::string_view("terminated");
}
namespace assets {
    inline constexpr std::string_view prefix = std::string_view("ludissey://asset/");
    inline constexpr std::array<std::string_view, 3> imageExtensions{std::string_view("png"), std::string_view("jpg"), std::string_view("jpeg")};
    inline constexpr std::array<std::string_view, 4> audioExtensions{std::string_view("mp3"), std::string_view("wav"), std::string_view("m4a"), std::string_view("aiff")};
    inline constexpr int megabytes = 8;
}

}
