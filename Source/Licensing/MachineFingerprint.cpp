#include "MachineFingerprint.h"

#include <cstring>

#if JUCE_MAC
 #include <CommonCrypto/CommonDigest.h>
 #include <IOKit/IOKitLib.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")
 #pragma comment(lib, "advapi32.lib")
#endif

namespace licensing
{
namespace
{
juce::String sha256Hex (const juce::MemoryBlock& data)
{
   #if JUCE_MAC
    unsigned char digest[CC_SHA256_DIGEST_LENGTH] {};
    CC_SHA256 (data.getData(), (CC_LONG) data.getSize(), digest);
   #elif JUCE_WINDOWS
    unsigned char digest[32] {};
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;
    DWORD hashSize = 0;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;

    if (BCryptOpenAlgorithmProvider (&algorithmHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    BCryptGetProperty (algorithmHandle, BCRYPT_OBJECT_LENGTH, (PUCHAR) &hashObjectSize, sizeof (hashObjectSize), &dataSize, 0);
    BCryptGetProperty (algorithmHandle, BCRYPT_HASH_LENGTH, (PUCHAR) &hashSize, sizeof (hashSize), &dataSize, 0);
    juce::HeapBlock<unsigned char> hashObject ((size_t) hashObjectSize);

    if (BCryptCreateHash (algorithmHandle, &hashHandle, hashObject.getData(), hashObjectSize, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider (algorithmHandle, 0);
        return {};
    }

    BCryptHashData (hashHandle, (PUCHAR) data.getData(), (ULONG) data.getSize(), 0);
    BCryptFinishHash (hashHandle, digest, (ULONG) hashSize, 0);
    BCryptDestroyHash (hashHandle);
    BCryptCloseAlgorithmProvider (algorithmHandle, 0);
   #else
    juce::ignoreUnused (data);
    return {};
   #endif

    juce::String hex;

    for (auto byte : digest)
        hex << juce::String::toHexString ((int) byte).paddedLeft ('0', 2);

    return hex;
}

#if JUCE_MAC
// Hardware-bound identifier that survives renames, reinstalls and network changes,
// unlike the Bonjour/computer name previously used here (which changes freely).
juce::String getStableMachineId()
{
    const auto platformExpert = IOServiceGetMatchingService (kIOMainPortDefault,
                                                               IOServiceMatching ("IOPlatformExpertDevice"));

    if (platformExpert == IO_OBJECT_NULL)
        return {};

    juce::String uuid;

    if (auto* uuidProperty = (CFStringRef) IORegistryEntryCreateCFProperty (
            platformExpert, CFSTR ("IOPlatformUUID"), kCFAllocatorDefault, 0))
    {
        uuid = juce::String::fromCFString (uuidProperty).trim();
        CFRelease (uuidProperty);
    }

    IOObjectRelease (platformExpert);

    if (uuid.isNotEmpty())
        return uuid;

    return {};
}
#elif JUCE_WINDOWS
juce::String getStableMachineId()
{
    HKEY key = nullptr;

    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                        KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS)
    {
        wchar_t buffer[64] {};
        DWORD size = sizeof (buffer);
        DWORD type = 0;
        const auto queried = RegQueryValueExW (key, L"MachineGuid", nullptr, &type,
                                                reinterpret_cast<LPBYTE> (buffer), &size);
        RegCloseKey (key);

        if (queried == ERROR_SUCCESS && type == REG_SZ)
        {
            const auto guid = juce::String (buffer).trim();

            if (guid.isNotEmpty())
                return guid;
        }
    }

    return {};
}
#endif
}

juce::String MachineFingerprint::createMachineIdHash()
{
    juce::StringArray parts;

   #if JUCE_MAC
    parts.add ("macOS");
   #elif JUCE_WINDOWS
    parts.add ("Windows");
   #else
    return {};
   #endif

    const auto stableMachineId = getStableMachineId();

    if (stableMachineId.isEmpty())
        return {};

    parts.add (stableMachineId);

    const auto raw = parts.joinIntoString ("|");
    const auto* utf8 = raw.toRawUTF8();
    juce::MemoryBlock bytes (utf8, std::strlen (utf8));
    return sha256Hex (bytes);
}
}
