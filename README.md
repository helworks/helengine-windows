# HelEngine Windows Host

This repository contains the native Windows host for HelEngine.

## Support Policy

- Supported runtime operating systems: `Windows 10` and `Windows 11`
- Supported build host operating systems: `Windows 10 x64` or newer
- Unsupported operating systems: `Windows 7`, `Windows 8`, and `Windows 8.1`

## Rendering Backend Policy

- Current Windows host direction: `DirectX 11`
- Planned later backend: `Vulkan`

The current repository only contains the DirectX11-side bootstrap scaffold. The real renderer implementation is still to be added.

## Generated Core Contract

This repository does not own generated engine C++ source.

Platform builds are expected to consume generated output from an external deployment root, using the generated source path passed into the native build configuration.
