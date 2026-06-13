using Microsoft.Win32;
using System;
using System.ServiceProcess;

public sealed class AllowContentService : RegistryNotificationServiceBase
{
    public AllowContentService()
        : base(
            "AllowContentAboveLockService",
            "AllowContentAboveLockService")
    {
    }

    protected override void ProcessAllKeys(RegistryKey baseKey, string sid)
    {
        foreach (string name in baseKey.GetSubKeyNames())
            ProcessOneKey(baseKey, sid, name);
    }

    private void ProcessOneKey(RegistryKey baseKey, string sid, string name)
    {
        try
        {
            using (RegistryKey subKey = baseKey.OpenSubKey(name, true))
            {
                if (subKey == null)
                    return;

                object value = subKey.GetValue("AllowContentAboveLock");
                int current = value == null ? 0 : Convert.ToInt32(value);
                if (current != 1)
                {
                    subKey.SetValue(
                        "AllowContentAboveLock",
                        1,
                        RegistryValueKind.DWord);
                    Log("Updated: " + sid + "\\" + name);
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
        ServiceBase.Run(new AllowContentService());
    }
}
