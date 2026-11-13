/*
 * Compiler memory helpers for freestanding plugins.
 *
 * Plugins are linked with -nostdlib; clang may still lower aggregate copies and
 * zero-initialisation to memcpy/memset. Defining them here keeps those calls
 * inside the module instead of turning them into unresolved env imports (the
 * core only registers the pime_host_* functions).
 *
 * The definitions are weak: native builds link the host C library, which wins
 * over these, and wasm builds resolve the calls locally.
 */
#ifndef PIMEPLUG_MEMORY_HPP
#define PIMEPLUG_MEMORY_HPP

#if defined(__wasm32__)

extern "C" {

__attribute__((weak)) void* memset(void* dst, int value, __SIZE_TYPE__ n) {
    unsigned char* out = static_cast<unsigned char*>(dst);
    const unsigned char byte = static_cast<unsigned char>(value);
    for (__SIZE_TYPE__ i = 0; i < n; ++i) out[i] = byte;
    return dst;
}

__attribute__((weak)) void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* out = static_cast<unsigned char*>(dst);
    const unsigned char* in = static_cast<const unsigned char*>(src);
    for (__SIZE_TYPE__ i = 0; i < n; ++i) out[i] = in[i];
    return dst;
}

__attribute__((weak)) void* memmove(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* out = static_cast<unsigned char*>(dst);
    const unsigned char* in = static_cast<const unsigned char*>(src);
    if (out < in) {
        for (__SIZE_TYPE__ i = 0; i < n; ++i) out[i] = in[i];
    } else if (out > in) {
        for (__SIZE_TYPE__ i = n; i > 0; --i) out[i - 1] = in[i - 1];
    }
    return dst;
}

}  // extern "C"

#endif  // __wasm32__

#endif  // PIMEPLUG_MEMORY_HPP
