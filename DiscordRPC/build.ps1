$ErrorActionPreference = "Stop"

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) {
    throw "C# compiler not found at $csc"
}

& $csc /nologo /optimize+ /target:exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /out:DiscordRPC.exe DiscordRPC.cs
