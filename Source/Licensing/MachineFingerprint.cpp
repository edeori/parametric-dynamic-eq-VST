#include "MachineFingerprint.h"

#include <cstring>
#include <juce_cryptography/juce_cryptography.h>
#if JUCE_MAC
 #include <IOKit/IOKitLib.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #pragma comment(lib, "advapi32.lib")
#endif

namespace licensing
{
namespace
{
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

    if (auto uuidProperty = IORegistryEntryCreateCFProperty (
            platformExpert, CFSTR ("IOPlatformUUID"), kCFAllocatorDefault, 0))
    {
        if (CFGetTypeID (uuidProperty) == CFStringGetTypeID())
            uuid = juce::String::fromCFString ((CFStringRef) uuidProperty).trim();
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
        const auto queried = RegGetValueW (key, nullptr, L"MachineGuid", RRF_RT_REG_SZ, &type,
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
    return juce::SHA256 (bytes).toHexString();
}
}
