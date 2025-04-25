#include <libhat/defines.hpp>

// Check for ARM or AArch64. Note: LIBHAT_ARM might need adjustment or use __arm__/__aarch64__ directly.
#if defined(LIBHAT_ARM) || defined(LIBHAT_AARCH64) // Assuming LIBHAT_AARCH64 will be defined for 64-bit ARM

#include <libhat/system.hpp>

#ifdef LIBHAT_LINUX // Only implement Linux detection for now
#include <sys/auxv.h>
// #include <asm/hwcap.h> // HWCAP_NEON might not be defined here for aarch64, use literal value instead
#endif

namespace hat {

#ifdef LIBHAT_LINUX
    static bool detect_neon() {
        unsigned long hwcap = getauxval(AT_HWCAP);
        // According to Linux kernel source (arch/arm64/include/uapi/asm/hwcap.h), HWCAP_NEON is (1 << 1)
        constexpr unsigned long NEON_BIT = (1 << 1);
        return (hwcap & NEON_BIT) != 0;
        // Note: For AArch64, NEON is mandatory according to ARMv8-A spec,
        // but checking HWCAP provides confirmation from the OS/kernel perspective.
    }
#else
    // Placeholder for other OS (Windows on ARM, macOS ARM)
    static bool detect_neon() {
        // TODO: Implement detection for other ARM platforms
        return false;
    }
#endif

    // Define the constructor for system_info_arm
    system_info_arm::system_info_arm() {
        // Call the detection function and store the result
        this->extensions.neon = detect_neon();

        // Initialize other system_info members if needed (e.g., page_size)
        // This might require calling OS-specific functions like sysconf(_SC_PAGESIZE)
        // For simplicity, inheriting the base constructor might handle this if it does detection.
        // Let's assume base constructor handles page_size for now.
    }

    // Remove the definitions of instance and get_system() for ARM here.
    // They are now defined conditionally in src/System.cpp
    // const system_info_arm system_info_arm::instance;
    // const system_info_arm& get_system() {
    //     return system_info_arm::instance;
    // }

} // namespace hat

#endif // ARM / AArch64 check
