# Microsoft WebView2 SDK subset

Pinned version: **1.0.4258.31**, Microsoft.Web.WebView2 NuGet package.
Official package: https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31
Download: https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg
Package SHA-256: 56F7F4B8BF9AEE4B8EFEFBBDD4F67D5F74EBD1B100ED0806DA71BF76AF481AA9

Only WebView2.h, LICENSE.txt and native x64/x86 WebView2LoaderStatic.lib files
were copied from this package, without modifications. Microsoft redistribution
terms are retained in LICENSE.txt. No Runtime/installer or external browser profile
is bundled. DesktopStub's x64 product recipe statically links its loader; inert
host fixtures exclude the embedded implementation and require no loader library.
Actual embedded sign-in requires the installed Evergreen WebView2 Runtime and
runs only after explicit user action. Its availability check provides guidance
without installing or launching an external browser.

| File | SHA-256 |
| --- | --- |
| LICENSE.txt | 0AF8F1B807512AAE39C2AC1AA4D0CAE65CABECB6FD554B8439A5162A0D6ECA55 |
| WebView2.h | DFF1E3181EC7EC203A34EF6EFA966590E0EF0BA1A5C3FE3B69DA6508C2F8A02E |
| x64/WebView2LoaderStatic.lib | E66284970876A616C82E7DCDA75A17FD46D04827F9DE81D4299B8862D34D25D6 |
| x86/WebView2LoaderStatic.lib | BD66D0B41302E6D56938E61DED16E32BC66318FD54324B0DAE419AEECCFB9B96 |
