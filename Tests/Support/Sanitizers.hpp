#pragma once

namespace support {

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
inline constexpr bool instrumented = true;
#else
inline constexpr bool instrumented = false;
#endif
#else
inline constexpr bool instrumented = false;
#endif

}
