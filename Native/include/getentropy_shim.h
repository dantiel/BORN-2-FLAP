// getentropy() shim for Android API < 28.
//
// wasm3's m3_api_wasi_posix.h calls getentropy() to implement WASI
// random_get, but bionic only provides getentropy() from API 28.
// arc4random_buf() is available in all API levels (since API 21) and
// fills the buffer with cryptographically-secure random bytes without
// failing — matching getentropy()'s contract (0 on success).
//
// Guarded so that on API >= 28 (where <unistd.h> declares getentropy)
// this is a no-op and the system declaration is used instead.
#if defined(__ANDROID__) && defined(__ANDROID_API__) && (__ANDROID_API__ < 28)
#include <stddef.h>
#include <stdlib.h>
static inline int getentropy(void* buf, size_t len) {
    arc4random_buf(buf, len);
    return 0;
}
#endif
