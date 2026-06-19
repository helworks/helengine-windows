# Windows Docker Build

There is no Docker build flow for the Windows host at this time.

Build Windows output through the shared platform wrapper instead:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ..\helengine\artifacts\build-platform.ps1 `
  -Project ..\helprojs\city\project.heproj `
  -Platform windows `
  -Output ..\helprojs\city\windows-build
```
