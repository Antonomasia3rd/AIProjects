using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.ServiceProcess;
using System.Text;
using System.Threading;

internal sealed class ManagedPrivilegedPathPin : IDisposable
{
    private readonly List<SafeFileHandle> handles;

    internal ManagedPrivilegedPathPin(
        List<SafeFileHandle> handles,
        string finalPath)
    {
        this.handles = handles;
        FinalPath = finalPath;
    }

    internal string FinalPath { get; private set; }

    internal string ReadAllText(int maximumBytes)
    {
        if (maximumBytes <= 0)
            throw new ArgumentOutOfRangeException("maximumBytes");
        if (maximumBytes == Int32.MaxValue)
            throw new ArgumentOutOfRangeException("maximumBytes");
        if (handles.Count == 0)
            throw new ObjectDisposedException("ManagedPrivilegedPathPin");

        SafeFileHandle leaf = handles[handles.Count - 1];
        bool addedReference = false;
        try
        {
            leaf.DangerousAddRef(ref addedReference);
            using (SafeFileHandle borrowed = new SafeFileHandle(
                leaf.DangerousGetHandle(),
                false))
            using (FileStream stream = new FileStream(
                borrowed,
                FileAccess.Read,
                4096,
                false))
            {
                if (stream.Length > maximumBytes)
                {
                    throw new InvalidOperationException(
                        "The protected service configuration exceeds " +
                        maximumBytes + " bytes.");
                }

                stream.Position = 0;
                byte[] contents = new byte[maximumBytes + 1];
                int total = 0;
                while (total < contents.Length)
                {
                    int read = stream.Read(
                        contents,
                        total,
                        contents.Length - total);
                    if (read == 0)
                        break;
                    total += read;
                }
                if (total > maximumBytes)
                {
                    throw new InvalidOperationException(
                        "The protected service configuration exceeds " +
                        maximumBytes + " bytes.");
                }

                using (MemoryStream memory = new MemoryStream(
                    contents,
                    0,
                    total,
                    false,
                    true))
                using (StreamReader reader = new StreamReader(
                    memory,
                    new UTF8Encoding(false, true),
                    true,
                    4096,
                    false))
                {
                    return reader.ReadToEnd();
                }
            }
        }
        finally
        {
            if (addedReference)
                leaf.DangerousRelease();
        }
    }

    public void Dispose()
    {
        for (int index = handles.Count - 1; index >= 0; --index)
            handles[index].Dispose();
        handles.Clear();
        FinalPath = null;
    }
}

internal static class ManagedPrivilegedPathTrust
{
    private const uint FileReadData = 0x00000001;
    private const uint FileWriteData = 0x00000002;
    private const uint FileAppendData = 0x00000004;
    private const uint FileWriteEa = 0x00000010;
    private const uint FileAddSubdirectory = 0x00000004;
    private const uint FileDeleteChild = 0x00000040;
    private const uint FileReadAttributes = 0x00000080;
    private const uint FileWriteAttributes = 0x00000100;
    private const uint DeleteAccess = 0x00010000;
    private const uint ReadControl = 0x00020000;
    private const uint WriteDac = 0x00040000;
    private const uint WriteOwner = 0x00080000;
    private const uint GenericRead = 0x80000000;
    private const uint GenericWrite = 0x40000000;
    private const uint GenericAll = 0x10000000;
    private const uint FileShareRead = 0x00000001;
    private const uint FileShareWrite = 0x00000002;
    private const uint FileShareDelete = 0x00000004;
    private const uint OpenExisting = 3;
    private const uint FileAttributeDirectory = 0x00000010;
    private const uint FileAttributeReparsePoint = 0x00000400;
    private const uint FileFlagOpenReparsePoint = 0x00200000;
    private const uint FileFlagBackupSemantics = 0x02000000;
    private const uint FileNameNormalized = 0x0;
    private const uint VolumeNameDos = 0x0;
    private const uint OwnerSecurityInformation = 0x00000001;
    private const uint DaclSecurityInformation = 0x00000004;
    private const uint ErrorSuccess = 0;
    private const int SeFileObject = 1;

    private static readonly SecurityIdentifier LocalSystemSid =
        new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null);
    private static readonly SecurityIdentifier AdministratorsSid =
        new SecurityIdentifier(
            WellKnownSidType.BuiltinAdministratorsSid,
            null);
    private static readonly SecurityIdentifier TrustedInstallerSid =
        new SecurityIdentifier(
            "S-1-5-80-956008885-3418522649-1831038044-" +
            "1853292631-2271478464");

    [StructLayout(LayoutKind.Sequential)]
    private struct ByHandleFileInformation
    {
        internal uint FileAttributes;
        internal System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        internal uint VolumeSerialNumber;
        internal uint FileSizeHigh;
        internal uint FileSizeLow;
        internal uint NumberOfLinks;
        internal uint FileIndexHigh;
        internal uint FileIndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(
        SafeFileHandle file,
        out ByHandleFileInformation information);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandleW(
        SafeFileHandle file,
        StringBuilder path,
        uint pathLength,
        uint flags);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern uint GetSecurityInfo(
        IntPtr handle,
        int objectType,
        uint securityInformation,
        out IntPtr owner,
        out IntPtr group,
        out IntPtr dacl,
        out IntPtr sacl,
        out IntPtr securityDescriptor);

    [DllImport("advapi32.dll")]
    private static extern uint GetSecurityDescriptorLength(
        IntPtr securityDescriptor);

    [DllImport("kernel32.dll")]
    private static extern IntPtr LocalFree(IntPtr memory);

    [DllImport("shell32.dll")]
    private static extern int SHGetKnownFolderPath(
        [MarshalAs(UnmanagedType.LPStruct)] Guid folderId,
        uint flags,
        IntPtr token,
        out IntPtr path);

    internal static bool TryOpenProgramFilesFile(
        string path,
        string label,
        out ManagedPrivilegedPathPin pin,
        out string error)
    {
        pin = null;
        error = null;
        string safeLabel = String.IsNullOrWhiteSpace(label)
            ? "privileged file"
            : label;

        string fullPath;
        if (!TryNormalizeAbsolutePath(path, out fullPath))
        {
            error = safeLabel +
                " must use a local absolute path without alternate streams: " +
                (path ?? "<null>");
            return false;
        }

        string trustedRoot;
        if (!TryFindProgramFilesRoot(fullPath, out trustedRoot))
        {
            error = "Refusing " + safeLabel +
                " outside the Program Files directories: " + fullPath;
            return false;
        }

        List<SafeFileHandle> handles = new List<SafeFileHandle>();
        try
        {
            string current = fullPath.Substring(0, 3);
            int cursor = 3;
            for (;;)
            {
                bool isLeaf = String.Equals(
                    current,
                    fullPath,
                    StringComparison.OrdinalIgnoreCase);
                bool isDirectory = !isLeaf;
                uint access = FileReadAttributes | ReadControl;
                if (isLeaf)
                    access |= GenericRead;
                uint flags = FileFlagOpenReparsePoint;
                if (isDirectory)
                    flags |= FileFlagBackupSemantics;

                SafeFileHandle handle = CreateFileW(
                    current,
                    access,
                    FileShareRead,
                    IntPtr.Zero,
                    OpenExisting,
                    flags,
                    IntPtr.Zero);
                if (handle.IsInvalid)
                {
                    int code = Marshal.GetLastWin32Error();
                    handle.Dispose();
                    error = "Could not pin " + safeLabel +
                        " path component: " + current +
                        " (error " + code + ")";
                    return false;
                }

                ByHandleFileInformation information;
                if (!GetFileInformationByHandle(handle, out information) ||
                    (((information.FileAttributes & FileAttributeDirectory) != 0) !=
                        isDirectory))
                {
                    handle.Dispose();
                    error = "Refusing wrong-type " + safeLabel +
                        " path component: " + current;
                    return false;
                }
                if ((information.FileAttributes & FileAttributeReparsePoint) != 0)
                {
                    handle.Dispose();
                    error = "Refusing reparse-point " + safeLabel +
                        " path component: " + current;
                    return false;
                }

                string finalPath;
                if (!TryGetFinalPath(handle, out finalPath) ||
                    !String.Equals(
                        TrimTrailingSeparators(finalPath),
                        TrimTrailingSeparators(current),
                        StringComparison.OrdinalIgnoreCase))
                {
                    handle.Dispose();
                    error = "Refusing redirected or non-canonical " +
                        safeLabel + " path component: " + current;
                    return false;
                }

                uint dangerous = DeleteAccess | WriteDac | WriteOwner |
                    FileWriteEa | FileWriteAttributes;
                if (isDirectory)
                {
                    dangerous |= FileDeleteChild;
                    if (IsAtOrBelow(current, trustedRoot))
                    {
                        dangerous |= FileWriteData | FileAddSubdirectory;
                    }
                }
                else
                {
                    dangerous |= FileWriteData | FileAppendData;
                }

                if (!HasProtectedOwnerAndDacl(
                    handle,
                    current,
                    safeLabel,
                    dangerous,
                    out error))
                {
                    handle.Dispose();
                    return false;
                }

                handles.Add(handle);
                if (isLeaf)
                {
                    if (!IsAtOrBelow(finalPath, trustedRoot))
                    {
                        error = "Refusing " + safeLabel +
                            " whose final path leaves Program Files: " +
                            finalPath;
                        return false;
                    }
                    pin = new ManagedPrivilegedPathPin(handles, finalPath);
                    handles = null;
                    return true;
                }

                while (cursor < fullPath.Length && fullPath[cursor] == '\\')
                    ++cursor;
                int separator = fullPath.IndexOf('\\', cursor);
                cursor = separator < 0 ? fullPath.Length : separator;
                current = fullPath.Substring(0, cursor);
            }
        }
        finally
        {
            if (handles != null)
            {
                for (int index = handles.Count - 1; index >= 0; --index)
                    handles[index].Dispose();
            }
        }
    }

    internal static bool IsCurrentProcessLocalSystem()
    {
        using (WindowsIdentity identity = WindowsIdentity.GetCurrent())
        {
            return identity.User != null && identity.User.Equals(LocalSystemSid);
        }
    }

    internal static string CurrentExecutablePath()
    {
        StringBuilder path = new StringBuilder(1024);
        for (;;)
        {
            uint length = GetModuleFileNameW(
                IntPtr.Zero,
                path,
                (uint)path.Capacity);
            if (length == 0)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if (length < path.Capacity - 1)
                return path.ToString();
            if (path.Capacity >= 32768)
                throw new InvalidOperationException(
                    "The service executable path is too long.");
            path.Capacity = Math.Min(32768, path.Capacity * 2);
        }
    }

    internal static string ConfigPathForExecutable(string executablePath)
    {
        return Path.ChangeExtension(executablePath, ".ini");
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetModuleFileNameW(
        IntPtr module,
        StringBuilder path,
        uint size);

    private static bool TryNormalizeAbsolutePath(
        string path,
        out string fullPath)
    {
        fullPath = null;
        if (String.IsNullOrWhiteSpace(path) ||
            path.Length < 3 ||
            !Char.IsLetter(path[0]) ||
            path[1] != ':' ||
            (path[2] != '\\' && path[2] != '/') ||
            path.IndexOf(':', 2) >= 0 ||
            path.StartsWith("\\\\?\\", StringComparison.Ordinal))
        {
            return false;
        }

        try
        {
            fullPath = Path.GetFullPath(path).Replace('/', '\\');
            return fullPath.Length >= 3 &&
                Char.IsLetter(fullPath[0]) &&
                fullPath[1] == ':' &&
                fullPath[2] == '\\' &&
                fullPath.IndexOf(':', 2) < 0;
        }
        catch (Exception ex)
        {
            if (ex is ArgumentException ||
                ex is NotSupportedException ||
                ex is PathTooLongException)
            {
                return false;
            }
            throw;
        }
    }

    private static bool TryFindProgramFilesRoot(
        string path,
        out string root)
    {
        root = null;
        foreach (string candidate in GetProgramFilesRoots())
        {
            if (IsAtOrBelow(path, candidate) &&
                (root == null || candidate.Length > root.Length))
            {
                root = candidate;
            }
        }
        return root != null;
    }

    private static IEnumerable<string> GetProgramFilesRoots()
    {
        Guid[] folderIds = new Guid[]
        {
            new Guid("905e63b6-c1bf-494e-b29c-65b732d3d21a"),
            new Guid("7c5a40ef-a0fb-4bfc-874a-c0f2e0b9fa8e"),
            new Guid("6d809377-6af0-444b-8957-a3773f02200e"),
        };
        List<string> roots = new List<string>();
        foreach (Guid folderId in folderIds)
        {
            IntPtr rawPath = IntPtr.Zero;
            try
            {
                if (SHGetKnownFolderPath(folderId, 0, IntPtr.Zero, out rawPath) != 0 ||
                    rawPath == IntPtr.Zero)
                {
                    continue;
                }

                string path = Marshal.PtrToStringUni(rawPath);
                string canonical;
                if (TryCanonicalExistingDirectory(path, out canonical) &&
                    !roots.Exists(value => String.Equals(
                        value,
                        canonical,
                        StringComparison.OrdinalIgnoreCase)))
                {
                    roots.Add(canonical);
                }
            }
            finally
            {
                if (rawPath != IntPtr.Zero)
                    Marshal.FreeCoTaskMem(rawPath);
            }
        }
        return roots;
    }

    private static bool TryCanonicalExistingDirectory(
        string path,
        out string canonical)
    {
        canonical = null;
        string fullPath;
        if (!TryNormalizeAbsolutePath(path, out fullPath))
            return false;
        fullPath = TrimTrailingSeparators(fullPath);

        using (SafeFileHandle handle = CreateFileW(
            fullPath,
            FileReadAttributes | ReadControl,
            FileShareRead | FileShareWrite | FileShareDelete,
            IntPtr.Zero,
            OpenExisting,
            FileFlagBackupSemantics | FileFlagOpenReparsePoint,
            IntPtr.Zero))
        {
            if (handle.IsInvalid)
                return false;
            ByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information) ||
                (information.FileAttributes & FileAttributeDirectory) == 0 ||
                (information.FileAttributes & FileAttributeReparsePoint) != 0 ||
                !TryGetFinalPath(handle, out canonical))
            {
                canonical = null;
                return false;
            }
        }
        canonical = TrimTrailingSeparators(canonical);
        return true;
    }

    private static bool TryGetFinalPath(
        SafeFileHandle handle,
        out string finalPath)
    {
        finalPath = null;
        uint needed = GetFinalPathNameByHandleW(
            handle,
            null,
            0,
            FileNameNormalized | VolumeNameDos);
        if (needed == 0 || needed > 32768)
            return false;
        StringBuilder buffer = new StringBuilder((int)needed + 1);
        uint written = GetFinalPathNameByHandleW(
            handle,
            buffer,
            (uint)buffer.Capacity,
            FileNameNormalized | VolumeNameDos);
        if (written == 0 || written >= buffer.Capacity)
            return false;

        string value = buffer.ToString().Replace('/', '\\');
        if (value.StartsWith("\\\\?\\", StringComparison.Ordinal))
            value = value.Substring(4);
        string normalized;
        if (!TryNormalizeAbsolutePath(value, out normalized))
            return false;
        finalPath = normalized;
        return true;
    }

    private static bool HasProtectedOwnerAndDacl(
        SafeFileHandle handle,
        string path,
        string label,
        uint dangerous,
        out string error)
    {
        error = null;
        IntPtr owner;
        IntPtr group;
        IntPtr dacl;
        IntPtr sacl;
        IntPtr descriptor;
        uint status = GetSecurityInfo(
            handle.DangerousGetHandle(),
            SeFileObject,
            OwnerSecurityInformation | DaclSecurityInformation,
            out owner,
            out group,
            out dacl,
            out sacl,
            out descriptor);
        if (status != ErrorSuccess || descriptor == IntPtr.Zero)
        {
            error = "Could not inspect " + label + " security: " +
                path + " (error " + status + ")";
            return false;
        }

        try
        {
            uint length = GetSecurityDescriptorLength(descriptor);
            if (length == 0 || length > 1024 * 1024)
            {
                error = "Refusing malformed " + label +
                    " security descriptor: " + path;
                return false;
            }
            byte[] bytes = new byte[length];
            Marshal.Copy(descriptor, bytes, 0, (int)length);
            RawSecurityDescriptor raw = new RawSecurityDescriptor(bytes, 0);
            if (!IsTrustedWriter(raw.Owner))
            {
                error = "Refusing user-owned or unrecognized-owner " +
                    label + ": " + path;
                return false;
            }
            if (raw.DiscretionaryAcl == null)
            {
                error = "Refusing " + label +
                    " with an unrestricted DACL: " + path;
                return false;
            }

            foreach (GenericAce ace in raw.DiscretionaryAcl)
            {
                if ((ace.AceFlags & AceFlags.InheritOnly) != 0 ||
                    !CanGrantAccess(ace.AceType))
                {
                    continue;
                }

                KnownAce known = ace as KnownAce;
                if (known == null)
                {
                    error = "Refusing uninspectable access-allow entry on " +
                        label + ": " + path;
                    return false;
                }
                if (!HasDangerousWriteAccess(
                    unchecked((uint)known.AccessMask),
                    dangerous))
                {
                    continue;
                }
                if (!IsTrustedWriter(known.SecurityIdentifier))
                {
                    error = "Refusing " + label +
                        " writable by a non-privileged principal: " + path;
                    return false;
                }
            }
            return true;
        }
        catch (Exception ex)
        {
            error = "Could not inspect " + label + " DACL: " + path +
                " (" + ex.Message + ")";
            return false;
        }
        finally
        {
            LocalFree(descriptor);
        }
    }

    private static bool IsTrustedWriter(SecurityIdentifier sid)
    {
        return sid != null &&
            (sid.Equals(LocalSystemSid) ||
                sid.Equals(AdministratorsSid) ||
                sid.Equals(TrustedInstallerSid));
    }

    private static bool CanGrantAccess(AceType type)
    {
        return type == AceType.AccessAllowed ||
            type == AceType.AccessAllowedObject ||
            type == AceType.AccessAllowedCallback ||
            type == AceType.AccessAllowedCallbackObject ||
            type == AceType.AccessAllowedCompound;
    }

    private static bool HasDangerousWriteAccess(uint mask, uint dangerous)
    {
        if ((mask & (GenericWrite | GenericAll)) != 0)
            return true;
        return (mask & dangerous) != 0;
    }

    private static string TrimTrailingSeparators(string path)
    {
        while (path.Length > 3 && path[path.Length - 1] == '\\')
            path = path.Substring(0, path.Length - 1);
        return path;
    }

    private static bool IsAtOrBelow(string path, string root)
    {
        if (path.Length < root.Length ||
            !path.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        return path.Length == root.Length || path[root.Length] == '\\';
    }
}

internal static class ManagedServiceEventLog
{
    private const ushort EventLogErrorType = 0x0001;
    private const ushort EventLogWarningType = 0x0002;
    private const ushort EventLogInformationType = 0x0004;

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr RegisterEventSourceW(
        string server,
        string source);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ReportEventW(
        IntPtr eventLog,
        ushort type,
        ushort category,
        uint eventId,
        IntPtr userSid,
        ushort stringCount,
        uint dataSize,
        string[] strings,
        IntPtr rawData);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeregisterEventSource(IntPtr eventLog);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern void OutputDebugStringW(string output);

    internal static void Information(string source, string message)
    {
        Write(source, EventLogInformationType, message);
    }

    internal static void Warning(string source, string message)
    {
        Write(source, EventLogWarningType, message);
    }

    internal static void Error(string source, string message)
    {
        Write(source, EventLogErrorType, message);
    }

    private static void Write(string source, ushort type, string message)
    {
        string safeSource = String.IsNullOrWhiteSpace(source)
            ? "AIProjectsService"
            : source;
        string safeMessage = message ?? "";
        if (safeMessage.Length > 30000)
            safeMessage = safeMessage.Substring(0, 30000);

        OutputDebugStringW(safeSource + ": " + safeMessage);
        IntPtr eventSource = RegisterEventSourceW(null, safeSource);
        if (eventSource == IntPtr.Zero)
            return;
        try
        {
            ReportEventW(
                eventSource,
                type,
                0,
                1,
                IntPtr.Zero,
                1,
                0,
                new string[] { safeMessage },
                IntPtr.Zero);
        }
        finally
        {
            DeregisterEventSource(eventSource);
        }
    }
}

public static class ManagedPrivilegedServiceHost
{
    private const uint ScManagerConnect = 0x0001;
    private const uint ScManagerCreateService = 0x0002;
    private const uint ServiceQueryConfig = 0x0001;
    private const uint ServiceAllAccess = 0x000F01FF;
    private const uint DeleteAccess = 0x00010000;
    private const uint ServiceWin32OwnProcess = 0x00000010;
    private const uint ServiceAutoStart = 0x00000002;
    private const uint ServiceErrorNormal = 0x00000001;
    private const uint ServiceConfigDescription = 1;
    private const int ErrorServiceDoesNotExist = 1060;
    private const int ErrorServiceMarkedForDelete = 1072;
    private const int ErrorInsufficientBuffer = 122;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ServiceDescription
    {
        internal IntPtr Description;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct QueryServiceConfig
    {
        internal uint ServiceType;
        internal uint StartType;
        internal uint ErrorControl;
        internal IntPtr BinaryPathName;
        internal IntPtr LoadOrderGroup;
        internal uint TagId;
        internal IntPtr Dependencies;
        internal IntPtr ServiceStartName;
        internal IntPtr DisplayName;
    }

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr OpenSCManagerW(
        string machineName,
        string databaseName,
        uint desiredAccess);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateServiceW(
        IntPtr serviceManager,
        string serviceName,
        string displayName,
        uint desiredAccess,
        uint serviceType,
        uint startType,
        uint errorControl,
        string binaryPathName,
        string loadOrderGroup,
        IntPtr tagId,
        string dependencies,
        string serviceStartName,
        string password);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr OpenServiceW(
        IntPtr serviceManager,
        string serviceName,
        uint desiredAccess);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool ChangeServiceConfig2W(
        IntPtr service,
        uint infoLevel,
        ref ServiceDescription info);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryServiceConfigW(
        IntPtr service,
        IntPtr queryServiceConfig,
        uint bufferSize,
        out uint bytesNeeded);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteService(IntPtr service);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseServiceHandle(IntPtr handle);

    public static int Run(
        string[] args,
        string serviceName,
        string displayName,
        string description,
        Func<RegistryNotificationServiceBase> createService)
    {
        if (createService == null)
            throw new ArgumentNullException("createService");
        args = args ?? new string[0];

        if (args.Length == 0)
        {
            if (Environment.UserInteractive)
            {
                Console.Error.WriteLine(
                    serviceName +
                    " must be started by the Windows Service Control Manager.");
                PrintUsage(serviceName);
                return 2;
            }
            ServiceBase.Run(createService());
            return 0;
        }

        if (args.Length != 1)
        {
            Console.Error.WriteLine("Exactly one command is accepted.");
            PrintUsage(serviceName);
            return 2;
        }

        string command = args[0];
        if (IsOneOf(command, "--help", "-h", "/?"))
        {
            PrintUsage(serviceName);
            return 0;
        }
        if (IsOneOf(command, "--version", "-v"))
        {
            Assembly entry = Assembly.GetEntryAssembly();
            Version version = entry == null
                ? null
                : entry.GetName().Version;
            Console.WriteLine(
                serviceName + " " +
                (version == null ? "0.0.0.0" : version.ToString()));
            return 0;
        }

        try
        {
            if (IsOneOf(command, "--install", "/install"))
            {
                Install(serviceName, displayName, description);
                Console.WriteLine(
                    serviceName +
                    " was installed. Start it through the Service Control Manager.");
                return 0;
            }
            if (IsOneOf(command, "--uninstall", "/uninstall"))
            {
                Uninstall(serviceName);
                return 0;
            }
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine(serviceName + ": " + ex.Message);
            return 1;
        }

        Console.Error.WriteLine("Unknown command: " + command);
        PrintUsage(serviceName);
        return 2;
    }

    private static void Install(
        string serviceName,
        string displayName,
        string description)
    {
        ManagedPrivilegedPathPin executable = null;
        ManagedPrivilegedPathPin configuration = null;
        try
        {
            string error;
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                ManagedPrivilegedPathTrust.CurrentExecutablePath(),
                "service executable",
                out executable,
                out error))
            {
                throw new InvalidOperationException(error);
            }
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                ManagedPrivilegedPathTrust.ConfigPathForExecutable(
                    executable.FinalPath),
                "service configuration",
                out configuration,
                out error))
            {
                throw new InvalidOperationException(error);
            }
            RegistryNotificationServiceBase.ReadLoggingSetting(configuration);

            IntPtr manager = OpenSCManagerW(
                null,
                null,
                ScManagerConnect | ScManagerCreateService);
            if (manager == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                string binaryPath = "\"" + executable.FinalPath + "\"";
                IntPtr service = CreateServiceW(
                    manager,
                    serviceName,
                    displayName,
                    ServiceAllAccess,
                    ServiceWin32OwnProcess,
                    ServiceAutoStart,
                    ServiceErrorNormal,
                    binaryPath,
                    null,
                    IntPtr.Zero,
                    null,
                    "LocalSystem",
                    null);
                if (service == IntPtr.Zero)
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                try
                {
                    try
                    {
                        IntPtr descriptionText = Marshal.StringToHGlobalUni(
                            description ?? "");
                        try
                        {
                            ServiceDescription serviceDescription =
                                new ServiceDescription();
                            serviceDescription.Description = descriptionText;
                            if (!ChangeServiceConfig2W(
                                service,
                                ServiceConfigDescription,
                                ref serviceDescription))
                            {
                                throw new Win32Exception(
                                    Marshal.GetLastWin32Error());
                            }
                        }
                        finally
                        {
                            Marshal.FreeHGlobal(descriptionText);
                        }
                    }
                    catch (Exception configurationError)
                    {
                        bool rollbackDeleted = DeleteService(service);
                        int rollbackError = rollbackDeleted
                            ? 0
                            : Marshal.GetLastWin32Error();
                        if (!rollbackDeleted &&
                            rollbackError != ErrorServiceMarkedForDelete)
                        {
                            throw new InvalidOperationException(
                                "Service creation succeeded, but post-creation " +
                                "configuration failed (" +
                                configurationError.Message + "). Rollback " +
                                "DeleteService also failed with error " +
                                rollbackError + " (" +
                                new Win32Exception(rollbackError).Message +
                                "). Manual service cleanup may be required.",
                                configurationError);
                        }
                        throw;
                    }
                }
                finally
                {
                    CloseServiceHandle(service);
                }
            }
            finally
            {
                CloseServiceHandle(manager);
            }
        }
        finally
        {
            if (configuration != null)
                configuration.Dispose();
            if (executable != null)
                executable.Dispose();
        }
    }

    private static void Uninstall(string serviceName)
    {
        ManagedPrivilegedPathPin executable = null;
        try
        {
            string error;
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                ManagedPrivilegedPathTrust.CurrentExecutablePath(),
                "service executable",
                out executable,
                out error))
            {
                throw new InvalidOperationException(error);
            }

            IntPtr manager = OpenSCManagerW(null, null, ScManagerConnect);
            if (manager == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                IntPtr service = OpenServiceW(
                    manager,
                    serviceName,
                    ServiceQueryConfig | DeleteAccess);
                if (service == IntPtr.Zero)
                {
                    int code = Marshal.GetLastWin32Error();
                    if (code == ErrorServiceDoesNotExist)
                    {
                        Console.WriteLine(serviceName + " is not installed.");
                        return;
                    }
                    throw new Win32Exception(code);
                }
                try
                {
                    ValidateRegisteredService(service, executable.FinalPath);
                    if (!DeleteService(service))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    Console.WriteLine(
                        serviceName +
                        " was marked for deletion. Stop it first if removal is pending.");
                }
                finally
                {
                    CloseServiceHandle(service);
                }
            }
            finally
            {
                CloseServiceHandle(manager);
            }
        }
        finally
        {
            if (executable != null)
                executable.Dispose();
        }
    }

    private static void ValidateRegisteredService(
        IntPtr service,
        string expectedExecutable)
    {
        uint needed;
        QueryServiceConfigW(service, IntPtr.Zero, 0, out needed);
        int firstError = Marshal.GetLastWin32Error();
        if (needed == 0 || firstError != ErrorInsufficientBuffer)
            throw new Win32Exception(firstError);

        IntPtr buffer = Marshal.AllocHGlobal(checked((int)needed));
        try
        {
            if (!QueryServiceConfigW(service, buffer, needed, out needed))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            QueryServiceConfig config = (QueryServiceConfig)
                Marshal.PtrToStructure(buffer, typeof(QueryServiceConfig));
            string binaryPath = Marshal.PtrToStringUni(config.BinaryPathName);
            string account = Marshal.PtrToStringUni(config.ServiceStartName);
            string expectedBinaryPath = "\"" + expectedExecutable + "\"";
            if (!String.Equals(
                binaryPath,
                expectedBinaryPath,
                StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException(
                    "Refusing to delete a same-name service registered for " +
                    "a different executable.");
            }
            if (!String.Equals(
                account,
                "LocalSystem",
                StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException(
                    "Refusing to delete a same-name service registered for " +
                    "a different account.");
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
    }

    private static bool IsOneOf(string value, params string[] choices)
    {
        foreach (string choice in choices)
        {
            if (String.Equals(
                value,
                choice,
                StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }
        return false;
    }

    private static void PrintUsage(string serviceName)
    {
        Console.WriteLine("Usage: " + serviceName + " [command]");
        Console.WriteLine("  --help       Show this help without changing the system.");
        Console.WriteLine("  --version    Show the executable version.");
        Console.WriteLine("  --install    Register the protected Program Files copy.");
        Console.WriteLine("  --uninstall  Delete this executable's service registration.");
        Console.WriteLine();
        Console.WriteLine(
            "With no command, the executable accepts only Service Control Manager startup.");
        Console.WriteLine(
            "Installation requires the executable and sibling INI to already be protected files under Program Files.");
    }
}

public abstract class RegistryNotificationServiceBase : ServiceBase
{
    private const string NotificationSettingsPath =
        @"Software\Microsoft\Windows\CurrentVersion\Notifications\Settings";

    private readonly object watcherLock = new object();
    private readonly object logLock = new object();
    private readonly object trustLock = new object();
    private readonly List<Thread> watcherThreads = new List<Thread>();
    private readonly HashSet<string> watchedSids =
        new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private readonly HashSet<string> watchedRootSids =
        new HashSet<string>(StringComparer.OrdinalIgnoreCase);
    private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
    private volatile bool running;
    private volatile bool loggingEnabled = true;
    private ManagedPrivilegedPathPin executablePin;
    private ManagedPrivilegedPathPin configurationPin;

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern int RegNotifyChangeKeyValue(
        IntPtr hKey,
        bool watchSubtree,
        RegChangeNotifyFilter notifyFilter,
        IntPtr eventHandle,
        bool asynchronous);

    [Flags]
    private enum RegChangeNotifyFilter
    {
        Name = 1,
        Attributes = 2,
        LastSet = 4,
        Security = 8,
    }

    protected RegistryNotificationServiceBase(string serviceName)
    {
        if (String.IsNullOrWhiteSpace(serviceName))
            throw new ArgumentException("A service name is required.", "serviceName");

        ServiceName = serviceName;
        // ServiceBase defaults AutoLog to true. Its EventLog path may try to
        // register ServiceName as an event source, bypassing the native
        // ReportEvent-only diagnostics contract used by privileged services.
        AutoLog = false;
    }

    protected abstract void ProcessAllKeys(RegistryKey baseKey, string sid);

    protected override void OnStart(string[] args)
    {
        lock (watcherLock)
        {
            if (watcherThreads.Exists(thread => thread.IsAlive))
                throw new InvalidOperationException(
                    "Previous registry watcher threads are still stopping.");

            watcherThreads.Clear();
            watchedSids.Clear();
            watchedRootSids.Clear();
        }

        try
        {
            AcquireRuntimeTrust();
            stopEvent.Reset();
            running = true;
            Log("Service started");

            if (!StartWatcher(WatchUsersRoot))
                throw new InvalidOperationException(
                    "The HKEY_USERS watcher could not be started.");
        }
        catch (Exception ex)
        {
            ManagedServiceEventLog.Error(
                ServiceName,
                "Service startup failed: " + ex.Message);
            StopWatchers(10000);
            ReleaseRuntimeTrust();
            throw;
        }
    }

    protected override void OnStop()
    {
        Log("Service stopping...");
        try
        {
            StopWatchers(10000);
        }
        finally
        {
            ReleaseRuntimeTrust();
        }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
            ReleaseRuntimeTrust();
        base.Dispose(disposing);
    }

    private void StopWatchers(int timeoutMilliseconds)
    {
        running = false;
        stopEvent.Set();

        Thread[] threads;
        lock (watcherLock)
        {
            threads = watcherThreads.ToArray();
        }

        Stopwatch stopwatch = Stopwatch.StartNew();
        foreach (Thread thread in threads)
        {
            if (thread == Thread.CurrentThread || !thread.IsAlive)
                continue;

            int remaining = Math.Max(
                0,
                timeoutMilliseconds - (int)Math.Min(
                    Int32.MaxValue,
                    stopwatch.ElapsedMilliseconds));
            if (remaining == 0 || !thread.Join(remaining))
                break;
        }

        int remainingThreads;
        lock (watcherLock)
        {
            watcherThreads.RemoveAll(thread => !thread.IsAlive);
            remainingThreads = watcherThreads.Count;
        }
        if (remainingThreads > 0)
        {
            Log(
                "WARNING: " + remainingThreads +
                " registry watcher thread(s) did not stop within " +
                timeoutMilliseconds + " ms total.");
        }
    }

    private bool StartWatcher(ThreadStart action)
    {
        Thread thread = null;
        thread = new Thread(delegate()
        {
            try
            {
                action();
            }
            catch (Exception ex)
            {
                Log("Registry watcher terminated unexpectedly: " + ex);
            }
            finally
            {
                lock (watcherLock)
                {
                    watcherThreads.Remove(thread);
                }
            }
        });
        thread.IsBackground = true;

        lock (watcherLock)
        {
            if (!running)
                return false;

            watcherThreads.Add(thread);
            try
            {
                thread.Start();
            }
            catch
            {
                watcherThreads.Remove(thread);
                throw;
            }
        }
        return true;
    }

    private void WatchUsersRoot()
    {
        while (running)
        {
            try
            {
                using (RegistryKey users = Registry.Users)
                {
                    while (running)
                    {
                        using (ManualResetEvent changed = new ManualResetEvent(false))
                        {
                            int status = RegNotifyChangeKeyValue(
                                users.Handle.DangerousGetHandle(),
                                false,
                                RegChangeNotifyFilter.Name,
                                changed.SafeWaitHandle.DangerousGetHandle(),
                                true);

                            if (status != 0)
                            {
                                Log("RegNotifyChangeKeyValue(HKU) failed: " + status);
                                if (stopEvent.WaitOne(5000))
                                    return;
                                continue;
                            }

                            // Register first, then enumerate. A user hive loaded
                            // during enumeration will either be observed by the
                            // enumeration or leave this event signalled.
                            AttachToExistingUsers();

                            int signaled = WaitHandle.WaitAny(
                                new WaitHandle[] { stopEvent, changed });
                            if (signaled == 0 || !running)
                                return;
                        }

                        Log("HKEY_USERS changed; refreshing loaded-user watchers.");
                        AttachToExistingUsers();
                    }
                }
            }
            catch (Exception ex)
            {
                Log("HKEY_USERS watcher error: " + ex.Message);
                if (stopEvent.WaitOne(5000))
                    return;
            }
        }
    }

    private void AttachToExistingUsers()
    {
        string[] subKeys;
        try
        {
            subKeys = Registry.Users.GetSubKeyNames();
        }
        catch (Exception ex)
        {
            Log("Could not enumerate HKEY_USERS: " + ex.Message);
            return;
        }

        foreach (string sid in subKeys)
        {
            if (!running)
                return;
            if (IsLoadedUserSid(sid))
                TryAttachUser(sid);
        }
    }

    private static bool IsLoadedUserSid(string sid)
    {
        if (String.IsNullOrEmpty(sid))
            return false;
        if (sid.EndsWith("_Classes", StringComparison.OrdinalIgnoreCase))
            return false;
        if (sid.Equals(".DEFAULT", StringComparison.OrdinalIgnoreCase))
            return false;
        if (sid.Equals("S-1-5-18", StringComparison.OrdinalIgnoreCase) ||
            sid.Equals("S-1-5-19", StringComparison.OrdinalIgnoreCase) ||
            sid.Equals("S-1-5-20", StringComparison.OrdinalIgnoreCase))
            return false;

        return sid.StartsWith("S-1-5-21-", StringComparison.OrdinalIgnoreCase) ||
            sid.StartsWith("S-1-12-1-", StringComparison.OrdinalIgnoreCase);
    }

    private bool TryAttachUser(string sid)
    {
        RegistryKey key = null;
        bool registered = false;
        try
        {
            key = Registry.Users.OpenSubKey(
                sid + "\\" + NotificationSettingsPath,
                true);
            if (key == null)
            {
                EnsureRootWatcher(sid);
                return false;
            }

            lock (watcherLock)
            {
                if (watchedSids.Contains(sid))
                {
                    key.Dispose();
                    return true;
                }

                watchedSids.Add(sid);
                watchedRootSids.Remove(sid);
                registered = true;
            }

            if (!StartWatcher(delegate() { WatchUserKey(key, sid); }))
            {
                key.Dispose();
                lock (watcherLock)
                {
                    watchedSids.Remove(sid);
                }
                return false;
            }

            Log("Attached to " + sid);
            return true;
        }
        catch (Exception ex)
        {
            if (key != null)
                key.Dispose();
            if (registered)
            {
                lock (watcherLock)
                {
                    watchedSids.Remove(sid);
                }
            }

            Log("Attach error for " + sid + ": " + ex.Message);
            if (running)
                EnsureRootWatcher(sid);
            return false;
        }
    }

    private void EnsureRootWatcher(string sid)
    {
        lock (watcherLock)
        {
            if (!running ||
                watchedSids.Contains(sid) ||
                watchedRootSids.Contains(sid))
                return;

            watchedRootSids.Add(sid);
        }

        RegistryKey rootKey = null;
        try
        {
            rootKey = Registry.Users.OpenSubKey(sid, false);
            if (rootKey == null)
            {
                lock (watcherLock)
                {
                    watchedRootSids.Remove(sid);
                }
                return;
            }

            if (!StartWatcher(delegate() { WatchUserRoot(rootKey, sid); }))
            {
                rootKey.Dispose();
                lock (watcherLock)
                {
                    watchedRootSids.Remove(sid);
                }
                return;
            }

            Log("Watching user root for notification settings: " + sid);
        }
        catch (Exception ex)
        {
            if (rootKey != null)
                rootKey.Dispose();
            lock (watcherLock)
            {
                watchedRootSids.Remove(sid);
            }
            Log("Root watch error for " + sid + ": " + ex.Message);
        }
    }

    private void WatchUserRoot(RegistryKey rootKey, string sid)
    {
        try
        {
            while (running)
            {
                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        rootKey.Handle.DangerousGetHandle(),
                        true,
                        RegChangeNotifyFilter.Name,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true);

                    if (status != 0)
                    {
                        Log(
                            "RegNotifyChangeKeyValue(root) failed for " +
                            sid + ": " + status);
                        if (stopEvent.WaitOne(5000))
                            return;
                        continue;
                    }

                    // Register before probing the descendant path so creation
                    // cannot land between the probe and notification setup.
                    bool settingsExist;
                    using (RegistryKey existing = Registry.Users.OpenSubKey(
                        sid + "\\" + NotificationSettingsPath,
                        true))
                    {
                        settingsExist = existing != null;
                    }
                    if (settingsExist && TryAttachUser(sid))
                        return;

                    int signaled = WaitHandle.WaitAny(
                        new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                        return;
                }
            }
        }
        finally
        {
            rootKey.Dispose();
            lock (watcherLock)
            {
                watchedRootSids.Remove(sid);
            }
        }
    }

    private void WatchUserKey(RegistryKey baseKey, string sid)
    {
        try
        {
            while (running)
            {
                using (ManualResetEvent changed = new ManualResetEvent(false))
                {
                    int status = RegNotifyChangeKeyValue(
                        baseKey.Handle.DangerousGetHandle(),
                        true,
                        RegChangeNotifyFilter.Name |
                            RegChangeNotifyFilter.LastSet,
                        changed.SafeWaitHandle.DangerousGetHandle(),
                        true);

                    if (status != 0)
                    {
                        Log(
                            "RegNotifyChangeKeyValue failed for " +
                            sid + ": " + status);
                        break;
                    }

                    // Arm the notification before scanning. Changes made
                    // during the scan leave the event signalled and cause a
                    // second pass instead of being lost in the setup window.
                    ProcessAllKeys(baseKey, sid);
                    if (!running)
                        return;

                    int signaled = WaitHandle.WaitAny(
                        new WaitHandle[] { stopEvent, changed });
                    if (signaled == 0 || !running)
                        return;
                }

                Log("Change detected for " + sid);
            }
        }
        finally
        {
            baseKey.Dispose();
            lock (watcherLock)
            {
                watchedSids.Remove(sid);
            }

            if (running)
                EnsureRootWatcher(sid);
        }
    }

    protected internal static bool EnsureDwordValue(
        RegistryKey key,
        string valueName,
        int expectedValue)
    {
        if (key == null)
            throw new ArgumentNullException("key");
        if (String.IsNullOrEmpty(valueName))
            throw new ArgumentException("A registry value name is required.", "valueName");

        object existing = key.GetValue(
            valueName,
            null,
            RegistryValueOptions.DoNotExpandEnvironmentNames);
        if (existing is int &&
            (int)existing == expectedValue &&
            key.GetValueKind(valueName) == RegistryValueKind.DWord)
        {
            return false;
        }

        key.SetValue(valueName, expectedValue, RegistryValueKind.DWord);
        return true;
    }

    protected internal static bool EnsureStringValue(
        RegistryKey key,
        string valueName,
        string expectedValue)
    {
        if (key == null)
            throw new ArgumentNullException("key");
        if (String.IsNullOrEmpty(valueName))
            throw new ArgumentException("A registry value name is required.", "valueName");
        if (expectedValue == null)
            throw new ArgumentNullException("expectedValue");

        object existing = key.GetValue(
            valueName,
            null,
            RegistryValueOptions.DoNotExpandEnvironmentNames);
        if (existing is string &&
            String.Equals((string)existing, expectedValue, StringComparison.Ordinal) &&
            key.GetValueKind(valueName) == RegistryValueKind.String)
        {
            return false;
        }

        key.SetValue(valueName, expectedValue, RegistryValueKind.String);
        return true;
    }

    protected void Log(string message)
    {
        lock (logLock)
        {
            if (LooksLikeWarning(message))
            {
                ManagedServiceEventLog.Warning(ServiceName, message);
                return;
            }
            if (loggingEnabled)
                ManagedServiceEventLog.Information(ServiceName, message);
        }
    }

    internal static bool ReadLoggingSetting(
        ManagedPrivilegedPathPin configuration)
    {
        if (configuration == null)
            throw new ArgumentNullException("configuration");

        return ParseLoggingSetting(configuration.ReadAllText(64 * 1024));
    }

    internal static bool ParseLoggingSetting(string contents)
    {
        if (contents == null)
            throw new ArgumentNullException("contents");

        string currentSection = "";
        bool logging = true;
        using (StringReader reader = new StringReader(contents))
        {
            string rawLine;
            while ((rawLine = reader.ReadLine()) != null)
            {
                string line = rawLine.Trim();
                if (line.Length == 0 ||
                    line.StartsWith(";") ||
                    line.StartsWith("#"))
                    continue;

                bool startsSection = line.StartsWith("[");
                bool endsSection = line.EndsWith("]");
                if (startsSection || endsSection)
                {
                    if (!startsSection || !endsSection)
                    {
                        throw new InvalidOperationException(
                            "Malformed section header in the protected " +
                            "service configuration: " + line);
                    }
                    currentSection =
                        line.Substring(1, line.Length - 2).Trim();
                    continue;
                }
                if (!currentSection.Equals(
                    "Settings",
                    StringComparison.OrdinalIgnoreCase))
                    continue;

                int equals = line.IndexOf('=');
                if (equals <= 0)
                {
                    throw new InvalidOperationException(
                        "Malformed assignment in [Settings] in the protected " +
                        "service configuration: " + line);
                }

                string name = line.Substring(0, equals).Trim();
                if (!IsValidSettingName(name))
                {
                    throw new InvalidOperationException(
                        "Invalid setting name in [Settings] in the protected " +
                        "service configuration: " + name);
                }
                if (!name.Equals(
                    "LoggingEnabled",
                    StringComparison.OrdinalIgnoreCase))
                    continue;

                bool parsed;
                string value = line.Substring(equals + 1).Trim();
                if (!TryParseBool(value, out parsed))
                {
                    throw new InvalidOperationException(
                        "Invalid [Settings] LoggingEnabled value in " +
                        "the protected service configuration: " + value);
                }
                // Match the repository INI baseline: when an assignment is
                // duplicated, the last assignment is the effective value.
                logging = parsed;
            }
        }
        return logging;
    }

    private static bool IsValidSettingName(string name)
    {
        return !String.IsNullOrWhiteSpace(name) &&
            name.Equals(name.Trim(), StringComparison.Ordinal) &&
            !name.StartsWith(";") &&
            !name.StartsWith("#") &&
            name.IndexOfAny(new char[] {
                '\r', '\n', '\0', '=', '[', ']'
            }) < 0;
    }

    private static bool TryParseBool(string value, out bool parsed)
    {
        if (value == "1" ||
            value.Equals("true", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("on", StringComparison.OrdinalIgnoreCase))
        {
            parsed = true;
            return true;
        }
        if (value == "0" ||
            value.Equals("false", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("no", StringComparison.OrdinalIgnoreCase) ||
            value.Equals("off", StringComparison.OrdinalIgnoreCase))
        {
            parsed = false;
            return true;
        }

        parsed = false;
        return false;
    }

    private void AcquireRuntimeTrust()
    {
        if (!ManagedPrivilegedPathTrust.IsCurrentProcessLocalSystem())
        {
            throw new InvalidOperationException(
                "The service account is not LocalSystem.");
        }

        ManagedPrivilegedPathPin newExecutable = null;
        ManagedPrivilegedPathPin newConfiguration = null;
        try
        {
            string error;
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                ManagedPrivilegedPathTrust.CurrentExecutablePath(),
                "service executable",
                out newExecutable,
                out error))
            {
                throw new InvalidOperationException(error);
            }
            if (!ManagedPrivilegedPathTrust.TryOpenProgramFilesFile(
                ManagedPrivilegedPathTrust.ConfigPathForExecutable(
                    newExecutable.FinalPath),
                "service configuration",
                out newConfiguration,
                out error))
            {
                throw new InvalidOperationException(error);
            }
            bool newLoggingEnabled = ReadLoggingSetting(newConfiguration);

            lock (trustLock)
            {
                if (executablePin != null || configurationPin != null)
                {
                    throw new InvalidOperationException(
                        "The service trust boundary is already initialized.");
                }
                executablePin = newExecutable;
                configurationPin = newConfiguration;
                loggingEnabled = newLoggingEnabled;
                newExecutable = null;
                newConfiguration = null;
            }
        }
        finally
        {
            if (newConfiguration != null)
                newConfiguration.Dispose();
            if (newExecutable != null)
                newExecutable.Dispose();
        }
    }

    private void ReleaseRuntimeTrust()
    {
        lock (trustLock)
        {
            if (configurationPin != null)
            {
                configurationPin.Dispose();
                configurationPin = null;
            }
            if (executablePin != null)
            {
                executablePin.Dispose();
                executablePin = null;
            }
        }
    }

    private static bool LooksLikeWarning(string message)
    {
        if (String.IsNullOrEmpty(message))
            return false;
        return message.IndexOf("warning", StringComparison.OrdinalIgnoreCase) >= 0 ||
            message.IndexOf("error", StringComparison.OrdinalIgnoreCase) >= 0 ||
            message.IndexOf("failed", StringComparison.OrdinalIgnoreCase) >= 0 ||
            message.IndexOf("could not", StringComparison.OrdinalIgnoreCase) >= 0 ||
            message.IndexOf("terminated", StringComparison.OrdinalIgnoreCase) >= 0;
    }
}

public sealed class RegistryNotificationValuePolicy
{
    private enum PolicyValueKind
    {
        Dword,
        String,
    }

    private readonly PolicyValueKind kind;
    private readonly int dwordValue;
    private readonly string stringValue;

    private RegistryNotificationValuePolicy(
        string valueName,
        PolicyValueKind kind,
        int dwordValue,
        string stringValue)
    {
        if (System.String.IsNullOrWhiteSpace(valueName))
        {
            throw new ArgumentException(
                "A registry value name is required.",
                "valueName");
        }
        if (valueName.IndexOf('\0') >= 0)
        {
            throw new ArgumentException(
                "A registry value name cannot contain NUL.",
                "valueName");
        }

        ValueName = valueName;
        this.kind = kind;
        this.dwordValue = dwordValue;
        this.stringValue = stringValue;
    }

    public string ValueName { get; private set; }

    public static RegistryNotificationValuePolicy Dword(
        string valueName,
        int expectedValue)
    {
        return new RegistryNotificationValuePolicy(
            valueName,
            PolicyValueKind.Dword,
            expectedValue,
            null);
    }

    public static RegistryNotificationValuePolicy String(
        string valueName,
        string expectedValue)
    {
        if (expectedValue == null)
            throw new ArgumentNullException("expectedValue");
        return new RegistryNotificationValuePolicy(
            valueName,
            PolicyValueKind.String,
            0,
            expectedValue);
    }

    internal bool Ensure(RegistryKey key)
    {
        if (kind == PolicyValueKind.Dword)
        {
            return RegistryNotificationServiceBase.EnsureDwordValue(
                key,
                ValueName,
                dwordValue);
        }
        return RegistryNotificationServiceBase.EnsureStringValue(
            key,
            ValueName,
            stringValue);
    }
}

public sealed class RegistryNotificationPolicy
{
    private readonly string subKeyPrefix;
    private readonly RegistryNotificationValuePolicy[] values;

    public RegistryNotificationPolicy(
        string subKeyPrefix,
        params RegistryNotificationValuePolicy[] values)
    {
        if (values == null)
            throw new ArgumentNullException("values");
        if (values.Length == 0)
        {
            throw new ArgumentException(
                "At least one registry value policy is required.",
                "values");
        }

        this.values = (RegistryNotificationValuePolicy[])values.Clone();
        for (int index = 0; index < this.values.Length; ++index)
        {
            if (this.values[index] == null)
            {
                throw new ArgumentException(
                    "Registry value policies cannot contain null.",
                    "values");
            }
        }
        this.subKeyPrefix = subKeyPrefix ?? "";
    }

    internal RegistryNotificationValuePolicy[] Values
    {
        get { return values; }
    }

    internal bool Matches(string subKeyName)
    {
        if (subKeyName == null)
            return false;
        return subKeyPrefix.Length == 0 || subKeyName.StartsWith(
            subKeyPrefix,
            StringComparison.OrdinalIgnoreCase);
    }
}

public sealed class RegistryNotificationPolicyService :
    RegistryNotificationServiceBase
{
    private readonly RegistryNotificationPolicy policy;

    public RegistryNotificationPolicyService(
        string serviceName,
        RegistryNotificationPolicy policy)
        : base(serviceName)
    {
        if (policy == null)
            throw new ArgumentNullException("policy");
        this.policy = policy;
    }

    protected override void ProcessAllKeys(RegistryKey baseKey, string sid)
    {
        foreach (string name in baseKey.GetSubKeyNames())
        {
            if (policy.Matches(name))
                ProcessOneKey(baseKey, sid, name);
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

                foreach (RegistryNotificationValuePolicy value in policy.Values)
                {
                    try
                    {
                        if (value.Ensure(subKey))
                        {
                            Log(
                                "Updated " + value.ValueName + ": " +
                                sid + "\\" + name);
                        }
                    }
                    catch (Exception ex)
                    {
                        Log(
                            "Could not update " + value.ValueName + " for " +
                            sid + "\\" + name + ": " + ex.Message);
                    }
                }
            }
        }
        catch (Exception ex)
        {
            Log("Could not update " + sid + "\\" + name + ": " + ex.Message);
        }
    }
}
