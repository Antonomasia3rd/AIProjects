### Contents:
* DNSAutoUpdate.ps1

# DNSAutoUpdate
## Why was this made?
* i use "adds.server.local" as an Active Directory domain, but i want to use "server.local" to access IIS-published websites
* i moved my laptop to different places, and DNS entries other than the AD itself does not auto update, so this was made to attempt to fix that
## NOTE: this code was deprecated
i moved my entire Windows Server setup on a VM, which gave me a full-control (static IP) from Internal Switch itself,
so i can still access it ("server.local") even though i'm not on a home router
