#include "LicenseManager.h"

#if JUCE_MAC
 #include <IOKit/IOKitLib.h>
#endif

LicenseManager::LicenseManager()
{
    machineID = computeMachineID();

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Octane";
    opts.filenameSuffix      = ".settings";
    opts.folderName          = "Carbonated Audio";
    opts.osxLibrarySubFolder = "Application Support";

    properties = std::make_unique<juce::PropertiesFile>(opts);

    loadStoredActivation();
}

void LicenseManager::loadStoredActivation()
{
    const juce::ScopedLock sl(propertiesLock);

    auto storedKey       = properties->getValue("licenseKey", "");
    auto storedMachineID = properties->getValue("machineID", "");
    auto storedToken     = properties->getValue("activationToken", "");

    activated.store(storedKey.isNotEmpty()
                    && storedToken.isNotEmpty()
                    && storedMachineID == machineID,
                    std::memory_order_relaxed);
}

juce::String LicenseManager::computeMachineID()
{
    juce::String hwID;

#if JUCE_MAC
    io_service_t platformExpert = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice"));

    if (platformExpert != IO_OBJECT_NULL)
    {
        CFTypeRef uuidRef = IORegistryEntryCreateCFProperty(
            platformExpert, CFSTR("IOPlatformUUID"), kCFAllocatorDefault, 0);

        if (uuidRef != nullptr)
        {
            hwID = juce::String::fromCFString(static_cast<CFStringRef>(uuidRef));
            CFRelease(uuidRef);
        }

        IOObjectRelease(platformExpert);
    }
#elif JUCE_WINDOWS
    hwID = juce::WindowsRegistry::getValue(
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid");
#endif

    if (hwID.isEmpty())
    {
        hwID = juce::SystemStats::getComputerName() + "|"
             + juce::SystemStats::getLogonName();
    }

    return juce::SHA256(hwID.toRawUTF8(), hwID.getNumBytesAsUTF8())
           .toHexString()
           .substring(0, 32);
}

void LicenseManager::tryActivate(
    const juce::String& key,
    std::function<void(bool, const juce::String&)> callback)
{
    auto keyClean = key.removeCharacters("- ").toLowerCase();
    auto mid      = machineID;

    juce::Thread::launch([this, keyClean, mid, cb = std::move(callback)]()
    {
        juce::DynamicObject::Ptr jsonObj = new juce::DynamicObject();
        jsonObj->setProperty("key",       keyClean);
        jsonObj->setProperty("machineID", mid);

        juce::URL url("https://carbonatedaudio.com/.netlify/functions/activate-octane");
        url = url.withPOSTData(juce::JSON::toString(juce::var(jsonObj.get())));

        int statusCode = 0;
        auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
            .withExtraHeaders("Content-Type: application/json")
            .withConnectionTimeoutMs(15000)
            .withStatusCode(&statusCode);

        auto stream = url.createInputStream(options);

        bool success = false;
        juce::String message;

        if (stream != nullptr)
        {
            auto response = stream->readEntireStreamAsString();
            auto parsed   = juce::JSON::parse(response);

            if (auto* obj = parsed.getDynamicObject())
            {
                success = static_cast<bool>(obj->getProperty("success"));

                if (success)
                {
                    auto token = obj->getProperty("token").toString();

                    {
                        const juce::ScopedLock sl(propertiesLock);
                        properties->setValue("licenseKey",      keyClean);
                        properties->setValue("machineID",       mid);
                        properties->setValue("activationToken", token);
                        properties->saveIfNeeded();
                    }

                    activated.store(true, std::memory_order_relaxed);
                    message = "Activated successfully!";
                }
                else
                {
                    message = obj->getProperty("error").toString();
                    if (message.isEmpty())
                        message = "Activation failed";
                }
            }
            else
            {
                message = "Invalid server response";
            }
        }
        else
        {
            message = "Could not connect to activation server. Check your internet connection.";
        }

        juce::MessageManager::callAsync([cb, success, message]()
        {
            if (cb)
                cb(success, message);
        });
    });
}
