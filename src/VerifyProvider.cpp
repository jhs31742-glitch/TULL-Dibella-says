#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

std::string tdsFileSha(const wchar_t* filename)
{
    const auto path = std::filesystem::path(filename);
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024) {
        return {};
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return {};
    }

    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }
    std::array<unsigned char, 32> digest{};
    BCRYPT_HASH_HANDLE hash{};
    ULONG objectSize{}, received{};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &received, 0) < 0 ||
        received != sizeof(objectSize) || !objectSize) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::vector<unsigned char> object(objectSize);
    auto status = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0);
    if (status >= 0) {
        status = BCryptHashData(hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0);
        if (status >= 0) {
            status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        return {};
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto value : digest) {
        result += hex[value >> 4];
        result += hex[value & 15];
    }
    return result;
}
