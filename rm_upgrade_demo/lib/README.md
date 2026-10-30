# Prebuilt upgrade_client libraries

Compiled `upgrade_client` libraries from the `rm_upgrade` project, organised by
target platform:

```
lib/
├── linux-x86_64/   libupgrade_client.a, libupgrade_client.so
├── linux-arm64/    libupgrade_client.a, libupgrade_client.so
└── windows-x64/    upgrade_client_static.lib, upgrade_client.lib, upgrade_client.dll
```

Each folder holds the library built for that architecture. To link the demo
against one of these instead of a live `rm_upgrade/build`, point
`RM_UPGRADE_BUILD` at the folder, e.g.:

```bash
cmake -S . -B build -DRM_UPGRADE_BUILD=lib/linux-x86_64
```

The `windows-x64/` libraries must be produced on a Windows agent (see the
project README); they are not built on Linux.
