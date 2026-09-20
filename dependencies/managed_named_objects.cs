using System;
using System.Globalization;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;

namespace AIProjects.Dependencies
{
    // Named kernel objects use the Global namespace so one user's copies of an
    // app coordinate across Remote Desktop and fast-user-switching sessions.
    // User-scoped names include a one-way SID-derived suffix so unrelated users
    // neither block nor signal one another.
    public static class ManagedNamedObjects
    {
        public static string CurrentUserScopedName(string component, string identity)
        {
            return @"Global\AIProjects." + NormalizeComponent(component) + "." +
                StableIdentityHash(CurrentUserIdentity()) + "." +
                StableIdentityHash(identity ?? "");
        }

        public static string MachineScopedName(string component, string identity)
        {
            return @"Global\AIProjects." + NormalizeComponent(component) + "." +
                StableIdentityHash(identity ?? "");
        }

        public static string StableIdentityHash(string value)
        {
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(value ?? ""));
                var text = new StringBuilder(16);
                for (int i = 0; i < 8; ++i)
                    text.Append(hash[i].ToString("X2", CultureInfo.InvariantCulture));
                return text.ToString();
            }
        }

        static string CurrentUserIdentity()
        {
            try
            {
                using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
                {
                    if (identity != null && identity.User != null &&
                        !String.IsNullOrWhiteSpace(identity.User.Value))
                        return "SID:" + identity.User.Value.ToUpperInvariant();
                }
            }
            catch
            {
                // Non-Windows test hosts and unusually restricted tokens can
                // lack a Windows SID. The fallback remains user-specific.
            }
            return "ACCOUNT:" +
                (Environment.UserDomainName ?? "").ToUpperInvariant() + "\\" +
                (Environment.UserName ?? "").ToUpperInvariant();
        }

        static string NormalizeComponent(string value)
        {
            if (String.IsNullOrWhiteSpace(value))
                throw new ArgumentException("A named-object component is required.", "component");
            string component = value.Trim();
            for (int i = 0; i < component.Length; ++i)
            {
                char character = component[i];
                if (!Char.IsLetterOrDigit(character) && character != '.' &&
                    character != '-' && character != '_')
                    throw new ArgumentException("The named-object component contains an invalid character.", "component");
            }
            return component;
        }
    }
}
