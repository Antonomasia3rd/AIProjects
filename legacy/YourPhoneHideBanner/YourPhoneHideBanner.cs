using System;
using System.Reflection;

[assembly: AssemblyTitle("YourPhoneHideBanner")]
[assembly: AssemblyProduct("AIProjects YourPhoneHideBanner")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

public static class YourPhoneHideBannerProgram
{
    private const string PhoneLinkNotificationPrefix =
        "Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_";
    private static readonly RegistryNotificationPolicy Policy =
        new RegistryNotificationPolicy(
            PhoneLinkNotificationPrefix,
            RegistryNotificationValuePolicy.Dword("ShowBanner", 0),
            RegistryNotificationValuePolicy.String("SoundFile", ""));

    public static int Main(string[] args)
    {
        return ManagedPrivilegedServiceHost.Run(
            args,
            "YourPhoneHideBannerService",
            "Hide Phone Link notification banners",
            "Suppresses matching Phone Link notification banners and sounds " +
                "for loaded users.",
            delegate()
            {
                return new RegistryNotificationPolicyService(
                    "YourPhoneHideBannerService",
                    Policy);
            });
    }
}
