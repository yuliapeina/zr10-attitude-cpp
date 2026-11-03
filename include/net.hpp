// net.hpp — UDP-сокети однаково на Linux (камера) і Windows (перевірка на ПК).
// На камері використовується лише Linux-частина; Windows-частина потрібна
// тільки для запуску з імітатором tools/fake_r2d2.py на вашому комп'ютері.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace zr10::net {

// Один раз підготувати мережу (на Windows — WSAStartup, на Linux нічого).
inline void init() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        done = true;
    }
#endif
}

// Створити UDP-сокет. Повертає -1, якщо не вдалося.
inline int udp_socket() {
    init();
#ifdef _WIN32
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return s == INVALID_SOCKET ? -1 : static_cast<int>(s);
#else
    return ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
}

inline void close_socket(int s) {
    if (s < 0) return;
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(s));
#else
    ::close(s);
#endif
}

// Надіслати пакет. Повертає true, якщо відправлено повністю.
inline bool send_to(int s, const void* data, int size, const sockaddr_in& to) {
    if (s < 0) return false;
#ifdef _WIN32
    const int n = ::sendto(static_cast<SOCKET>(s), static_cast<const char*>(data), size, 0,
                           reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#else
    const long n = ::sendto(s, data, static_cast<size_t>(size), 0,
                            reinterpret_cast<const sockaddr*>(&to), sizeof(to));
#endif
    return n == size;
}

// Зробити сокет неблокуючим (прийом не чекає, якщо даних немає).
inline bool set_nonblocking(int s) {
#ifdef _WIN32
    u_long on = 1;
    return ::ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on) == 0;
#else
    const int flags = ::fcntl(s, F_GETFL, 0);
    return flags >= 0 && ::fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// Прийняти пакет без очікування. Повертає кількість байт, 0 — даних немає,
// -1 — помилка. from заповнюється адресою відправника.
inline int recv_from(int s, void* buf, int size, sockaddr_in& from) {
    if (s < 0) return -1;
#ifdef _WIN32
    int len = sizeof(from);
    const int n = ::recvfrom(static_cast<SOCKET>(s), static_cast<char*>(buf), size, 0,
                             reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) {
        const int e = WSAGetLastError();
        // WSAECONNRESET: на Windows так приходить «порт закритий» від
        // попереднього пакета — не помилка для нас, просто даних немає.
        return (e == WSAEWOULDBLOCK || e == WSAECONNRESET) ? 0 : -1;
    }
    return n;
#else
    socklen_t len = sizeof(from);
    const long n = ::recvfrom(s, buf, static_cast<size_t>(size), 0,
                              reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) ? 0 : -1;
    }
    return static_cast<int>(n);
#endif
}

// Адреса ip:port. Некоректний ip → 127.0.0.1.
inline sockaddr_in make_addr(const char* ip, int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<unsigned short>(port));
    if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    return a;
}

}  // namespace zr10::net
