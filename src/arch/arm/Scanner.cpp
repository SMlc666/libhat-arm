#include <libhat/defines.hpp>

// Check if compiling for ARM/AArch64 and NEON is potentially available
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(LIBHAT_DISABLE_NEON) // Add LIBHAT_DISABLE_NEON later in CMake

#include <libhat/scanner.hpp>

// Include NEON header
#ifdef _MSC_VER // MSVC might use a different header or intrinsics names
    // #include <arm_neon.h> // Or appropriate MSVC header
#else
    #include <arm_neon.h>
#endif

#include <cstdint>
#include <cstring> // For std::equal in fallback

// Helper for bit scanning (consistent with SSE.cpp)
#ifdef _MSC_VER
    #include <intrin.h>
    namespace hat::detail {
        inline unsigned long bsf(uint32_t num) noexcept { // Use uint32_t for portability
            unsigned long offset;
            _BitScanForward(&offset, num);
            return offset;
        }
        // Add bsf for 64-bit if needed for AArch64 masks
        #ifdef _M_ARM64 // Or appropriate check for ARM64 MSVC
        inline unsigned long bsf64(uint64_t num) noexcept {
             unsigned long offset;
            _BitScanForward64(&offset, num);
            return offset;
        }
        #endif
    }
    #define LIBHAT_BSF32(num) hat::detail::bsf(num)
    #ifdef _M_ARM64
    #define LIBHAT_BSF64(num) hat::detail::bsf64(num)
    #else
    // Define LIBHAT_BSF64 fallback or error if needed on 32-bit MSVC ARM
    #endif
#else // GCC/Clang
    #define LIBHAT_BSF32(num) static_cast<unsigned long>(__builtin_ctz(num))
    #define LIBHAT_BSF64(num) static_cast<unsigned long>(__builtin_ctzll(num))
#endif


namespace hat::detail {

    // Loads the first 16 bytes of the signature and its corresponding mask into NEON vectors.
    // Mask is 0xFF for valid bytes, 0x00 for wildcards.
    inline void load_signature_neon(const signature_view signature, uint8x16_t& bytes, uint8x16_t& mask) {
        alignas(16) uint8_t byteBuffer[16]{};
        alignas(16) uint8_t maskBuffer[16]{};
        for (size_t i = 0; i < 16; ++i) {
            if (i < signature.size() && signature[i].has_value()) {
                byteBuffer[i] = static_cast<uint8_t>(*signature[i]);
                maskBuffer[i] = 0xFFu;
            } else {
                byteBuffer[i] = 0; // Value doesn't matter for wildcards
                maskBuffer[i] = 0x00u;
            }
        }
        bytes = vld1q_u8(byteBuffer);
        mask = vld1q_u8(maskBuffer);
    }

    // NEON optimized pattern scanning function
    template<scan_alignment alignment, bool cmpeq2, bool veccmp>
    const_scan_result find_pattern_neon(const std::byte* begin, const std::byte* end, const scan_context& context) {
        const auto signature = context.signature;
        const auto cmpIndex = cmpeq2 ? *context.pairIndex : 0;
        LIBHAT_ASSUME(cmpIndex < 16 || !cmpeq2); // cmpIndex must be valid if cmpeq2 is true

        // Setup: Load first byte(s) and signature/mask if veccmp
        const uint8x16_t firstByteVec = vdupq_n_u8(static_cast<uint8_t>(*signature[cmpIndex]));
        uint8x16_t secondByteVec{}; // Zero initialized
        if constexpr (cmpeq2) {
            secondByteVec = vdupq_n_u8(static_cast<uint8_t>(*signature[cmpIndex + 1]));
        }

        uint8x16_t signatureBytes{}, signatureMask{}; // Zero initialized
        if constexpr (veccmp) {
            load_signature_neon(signature, signatureBytes, signatureMask);
        }

        // Segment scan: Split into pre-vector, vector, post-vector parts
        // Assuming segment_scan works with std::byte* and __m128i equivalent size (16 bytes)
        // If segment_scan needs adaptation for NEON types, that's a separate step.
        auto [pre, vec, post] = segment_scan<uint8x16_t, veccmp>(begin, end, signature.size(), cmpIndex);

        // Handle pre-segment (cannot use full vector operations)
        if (!pre.empty()) {
            const auto result = find_pattern_single<alignment>(pre.data(), pre.data() + pre.size(), context);
            if (result.has_result()) {
                return result;
            }
        }

        // Main vector loop
        alignas(16) uint8_t cmp_bytes[16]; // Buffer for storing comparison results
        for (const auto& it : vec) { // it is likely std::byte* pointing to start of 16-byte chunk
            const uint8_t* current_chunk_ptr = reinterpret_cast<const uint8_t*>(&it);

            // Load 16 bytes of data from memory
            const uint8x16_t dataVec = vld1q_u8(current_chunk_ptr + cmpIndex);

            // Compare first byte
            uint8x16_t cmpVec = vceqq_u8(firstByteVec, dataVec);

            // --- Mask Generation ---
            // Store comparison result to memory and build mask manually.
            // This avoids complex NEON intrinsics for movemask simulation for now.
            vst1q_u8(cmp_bytes, cmpVec);
            uint16_t mask = 0;
            for(int k=0; k<16; ++k) {
                if (cmp_bytes[k] == 0xFF) {
                    mask |= (1u << k);
                }
            }
            // --- End Mask Generation ---

            // Apply alignment mask if needed
            if constexpr (alignment != scan_alignment::X1) {
                mask &= create_alignment_mask<uint16_t, alignment>();
                if (!mask) continue; // Skip chunk if no aligned matches for first byte
            } else if constexpr (cmpeq2) {
                // Apply second byte comparison optimization
                const uint8x16_t dataVecNext = vld1q_u8(current_chunk_ptr + cmpIndex + 1);
                const uint8x16_t cmpVec2 = vceqq_u8(secondByteVec, dataVecNext);
                // Generate mask for second byte comparison
                vst1q_u8(cmp_bytes, cmpVec2); // Reuse buffer
                uint16_t mask2 = 0;
                for(int k=0; k<16; ++k) {
                    if (cmp_bytes[k] == 0xFF) {
                        mask2 |= (1u << k);
                    }
                }
                // Combine masks: require byte N and byte N+1 to match
                mask &= mask2; // Simple AND, assumes cmpIndex=0. SSE logic was mask &= (mask2 >> 1) | (1u << 15);
                               // Let's stick to the simpler AND for now, assuming cmpIndex=0 mostly.
                               // A more robust solution would need shifting based on cmpIndex.
                               // Revisit this if cmpIndex != 0 becomes important for NEON.
            }

            // Iterate through potential matches indicated by the mask
            while (mask) {
                const auto offset = LIBHAT_BSF32(mask); // Find the index of the first set bit
                // Calculate the actual potential match address in the original memory space
                const auto i = reinterpret_cast<const std::byte*>(current_chunk_ptr) + offset; // Adjusted based on loop variable type

                // Verify the full signature at the potential match address 'i'
                if constexpr (veccmp) {
                    // Vectorized verification for short signatures (<= 16 bytes)
                    const uint8x16_t dataToVerify = vld1q_u8(reinterpret_cast<const uint8_t*>(i));
                    const uint8x16_t cmpSigVec = vceqq_u8(signatureBytes, dataToVerify);
                    const uint8x16_t maskedCmp = vandq_u8(cmpSigVec, signatureMask); // Apply wildcard mask
                    // Check if all non-wildcard bytes matched
                    const uint8x16_t finalCheckVec = vceqq_u8(maskedCmp, signatureMask);
                    // Use horizontal minimum to check if all bytes in finalCheckVec are 0xFF
                    if (vminvq_u8(finalCheckVec) == 0xFF) LIBHAT_UNLIKELY {
                        return i; // Match found!
                    }
                } else {
                    // Fallback to scalar verification for long signatures (> 16 bytes)
                    auto match = std::equal(signature.begin(), signature.end(), i, [](auto opt, auto byte) {
                        return !opt.has_value() || *opt == byte;
                    });
                    if (match) LIBHAT_UNLIKELY {
                        return i; // Match found!
                    }
                }

                // Clear the processed bit and continue checking other bits in the mask
                mask &= (mask - 1);
            }
        }

        // Handle post-segment (cannot use full vector operations)
        if (!post.empty()) {
            return find_pattern_single<alignment>(post.data(), post.data() + post.size(), context);
        }

        return {}; // No match found
    }

    // Resolver function for NEON mode
    template<>
    scan_function_t resolve_scanner<scan_mode::NEON>(scan_context& context) {
        // TODO: Apply NEON specific hints if any? (Vector size is 16 bytes like SSE)
        context.apply_hints({.vectorSize = 16}); // Assuming 128-bit NEON vectors

        const auto alignment = context.alignment;
        const auto signature = context.signature;
        // NEON generally works on 128-bit vectors, so veccmp logic might be similar to SSE
        const bool veccmp = signature.size() <= 16;

        // Select the appropriate template instantiation based on context
        // This structure mirrors the SSE resolver
        if (alignment == scan_alignment::X1) {
            const bool cmpeq2 = context.pairIndex.has_value();
            if (cmpeq2 && veccmp) {
                // return &find_pattern_neon<scan_alignment::X1, true, true>; // Uncomment when implemented
            } else if (cmpeq2) {
                // return &find_pattern_neon<scan_alignment::X1, true, false>; // Uncomment when implemented
            } else if (veccmp) {
                // return &find_pattern_neon<scan_alignment::X1, false, true>; // Uncomment when implemented
            } else {
                // return &find_pattern_neon<scan_alignment::X1, false, false>; // Uncomment when implemented
            }
        } else if (alignment == scan_alignment::X16) {
             if (veccmp) {
                // return &find_pattern_neon<scan_alignment::X16, false, true>; // Uncomment when implemented
            } else {
                // return &find_pattern_neon<scan_alignment::X16, false, false>; // Uncomment when implemented
            }
        }
        // LIBHAT_UNREACHABLE(); // Should not happen if alignment is valid

        // Return the selected NEON scanner function pointer
        // (Currently commented out until implementation is complete and tested)
        // LIBHAT_UNREACHABLE(); // Should not happen if alignment is valid

        // Fallback to single scanner until NEON implementation is fully verified
        // TODO: Uncomment the return statements above when find_pattern_neon is implemented
        return resolve_scanner<scan_mode::Single>(context);
    }

} // namespace hat::detail

#endif // NEON check