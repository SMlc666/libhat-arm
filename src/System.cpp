#include <libhat/system.hpp>

namespace hat {

    // Define the base class constructor.
    system_info::system_info() {
        // TODO: Implement common system info retrieval if needed, e.g., page size.
    }

    // Conditionally define the instance and get_system based on architecture
#if defined(LIBHAT_X86) || defined(LIBHAT_X86_64)
    // Define the static instance for x86. The constructor in arch/x86/System.cpp will initialize it.
    const system_info_x86 system_info_x86::instance;

    // Define the global get_system function for x86
    const system_info_x86& get_system() {
        return system_info_x86::instance;
    }
#elif defined(LIBHAT_ARM) || defined(LIBHAT_AARCH64)
    // Define the static instance for ARM. The constructor in arch/arm/System.cpp will initialize it.
    const system_info_arm system_info_arm::instance;

    // Define the global get_system function for ARM
    const system_info_arm& get_system() {
        return system_info_arm::instance;
    }
#else
    #error "Unsupported architecture: No system_info implementation selected"
    // Or provide a minimal default implementation if preferred over an error
    // const system_info system_info::instance; // Example minimal
    // const system_info& get_system() { return system_info::instance; }
#endif

} // namespace hat
