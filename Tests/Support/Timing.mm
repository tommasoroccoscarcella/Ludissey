#include "Tests/Support/Timing.hpp"

#import <Foundation/Foundation.h>

namespace support::timing {

bool lowPower() { return NSProcessInfo.processInfo.lowPowerModeEnabled; }

}
