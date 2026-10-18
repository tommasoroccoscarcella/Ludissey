#include "Engine/Core/LocalPreferences.hpp"
#include "Interface/Canvas.hpp"
#include "Interface/Theme.hpp"
#include "Localization/Localization.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <CoreGraphics/CoreGraphics.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

class Environment final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        ld::local_preferences::useMemory(true);
        ld::localization::setLanguage(ld::Language::italian);
        setenv("LUDISSEY_ROOT", LUDISSEY_SOURCE_ROOT, 0);
        if (!std::getenv("LUDISSEY_SUPPORT")) {
            folder = std::filesystem::temp_directory_path() / ("Ludissey Test " + std::to_string(getpid()));
            std::filesystem::create_directories(folder);
            setenv("LUDISSEY_SUPPORT", folder.c_str(), 1);
        }
        IOPMAssertionDeclareUserActivity(CFSTR("Test di Ludissey"), kIOPMUserActiveLocal, &activity);
        IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, CFSTR("Test di Ludissey"), &display);
        for (int i = 0; i < 20 && CGDisplayIsAsleep(CGMainDisplayID()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if (CGDisplayIsAsleep(CGMainDisplayID())) std::cout << "Schermo spento: i test con finestra reale non ricevono frame e le misure di tempo non sono valide.\n";
    }

    void testCaseStarting(const Catch::TestCaseInfo&) override {
        ld::localization::setLanguage(ld::Language::italian);
        ui::theme::applyScale(ui::theme::Scale::client);
    }

    void testRunEnded(const Catch::TestRunStats&) override {
        if (display) IOPMAssertionRelease(display);
        if (activity) IOPMAssertionRelease(activity);
        if (!folder.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(folder, ignored);
        }
    }

private:
    std::filesystem::path folder;
    IOPMAssertionID activity = 0;
    IOPMAssertionID display = 0;
};

}

CATCH_REGISTER_LISTENER(Environment)
