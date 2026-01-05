#ifndef ZNET_ENDIAN_H_
#define ZNET_ENDIAN_H_

#include <byteswap.h>
#include <type_traits>

namespace znet {

#define ZNET_LITTLE_ENDIAN 1
#define ZNET_BIG_ENDIAN    2

#if BYTE_ORDER == BIG_ENDIAN
#define ZNET_BYTE_ORDER ZNET_BIG_ENDIAN
#else
#define ZNET_BYTE_ORDER ZNET_LITTLE_ENDIAN
#endif

template <class T>
typename std::enable_if<
    std::is_integral<T>::value && sizeof(T) == 1,
    T>::type
byteswap(T v) {
    return v;
}

template <class T>
typename std::enable_if<
    std::is_integral<T>::value && sizeof(T) == 2,
    T>::type
byteswap(T v) {
    using U = typename std::make_unsigned<T>::type;
    return static_cast<T>(bswap_16(static_cast<U>(v)));
}

template <class T>
typename std::enable_if<
    std::is_integral<T>::value && sizeof(T) == 4,
    T>::type
byteswap(T v) {
    using U = typename std::make_unsigned<T>::type;
    return static_cast<T>(bswap_32(static_cast<U>(v)));
}

template <class T>
typename std::enable_if<
    std::is_integral<T>::value && sizeof(T) == 8,
    T>::type
byteswap(T v) {
    using U = typename std::make_unsigned<T>::type;
    return static_cast<T>(bswap_64(static_cast<U>(v)));
}

} // namespace znet

#endif // ZNET_ENDIAN_H_
