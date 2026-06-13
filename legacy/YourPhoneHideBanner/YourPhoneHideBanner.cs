using Microsoft.Win32;
using System;
using System.ServiceProcess;

public sealed class YourPhoneHideBannerService :
    RegistryNotificationServiceBase
{
    private const string PhoneLinkNotificationPrefix =
        "Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_";

    public YourPhoneHideBannerService()
        : base(
            "YourPhoneHideBannerService",
            "YourPhoneHideBannerService")
    {
    }

    protected override void ProcessAllKeys(RegistryKey baseKey, string sid)
    {
        foreach (string name in baseKey.GetSubKeyNames())
        {
            if (name.StartsWith(
                PhoneLinkNotificationPrefix,
                StringComparison.OrdinalIgnoreCase))
            {
                ProcessOneKey(baseKey, sid, name);
            }
        }
    }

    private void ProcessOneKey(RegistryKey baseKey, string sid, string name)
    {
        try
        {
            using (RegistryKey subKey = baseKey.OpenSubKey(name, true))
            {
                if (subKey == null)
                    return;

                object bannerObject = subKey.GetValue("ShowBanner");
                int bannerValue =
                    bannerObject == null ? -1 : Convert.ToInt32(bannerObject);
                if (bannerValue != 0)
                {
                    subKey.SetValue(
                        "ShowBanner",
                        0,
                        RegistryValueKind.DWord);
                    Log("Updated ShowBanner: " + sid + "\\" + name);
                }

                object soundObject = subKey.GetValue("SoundFile");
                string soundValue = soundObject as string;
                if (soundValue == null || soundValue.Length != 0)
                {
                    subKey.SetValue(
                        "SoundFile",
                        "",
                        RegistryValueKind.String);
                    Log("Updated SoundFile: " + sid + "\\" + name);
                }
            }
        }
        catch (Exception ex)
        {
            Log("Could not update " + sid + "\\" + name + ": " + ex.Message);
        }
    }

    public static void Main()
    {
        ServiceBase.Run(new YourPhoneHideBannerService());
    }
}
