#include <libhat/system.hpp>

int main() {
    const auto& system = hat::get_system();
    
#if defined(LIBHAT_X86) || defined(LIBHAT_X86_64) // Check if compiling for x86/x86_64
    printf("Platform: x86/x86_64\n");
    printf("cpu_vendor: %s\n", system.cpu_vendor.c_str());
    printf("cpu_brand: %s\n", system.cpu_brand.c_str());
    // extensions
    const auto& ext = system.extensions;
    printf("sse: %d\n", ext.sse);
    printf("sse2: %d\n", ext.sse2);
    printf("sse3: %d\n", ext.sse3);
    printf("ssse3: %d\n", ext.ssse3);
    printf("sse41: %d\n", ext.sse41);
    printf("sse42: %d\n", ext.sse42);
    printf("avx: %d\n", ext.avx);
    printf("avx2: %d\n", ext.avx2);
    printf("avx512f: %d\n", ext.avx512f);
    printf("avx512bw: %d\n", ext.avx512bw);
    printf("popcnt: %d\n", ext.popcnt);
    printf("bmi: %d\n", ext.bmi);
#elif defined(LIBHAT_ARM) || defined(LIBHAT_AARCH64) // Check if compiling for ARM/AArch64
    printf("Platform: ARM/AArch64\n");
    // Print ARM specific info
    const auto& ext = system.extensions;
    printf("neon: %d\n", ext.neon);
    // Add prints for other ARM extensions if they are added later (e.g., crypto, sve)
#else
    printf("Platform: Unknown or unsupported\n");
#endif

    return 0;
}
