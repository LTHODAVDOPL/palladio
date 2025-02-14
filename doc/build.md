# Build and Run Palladio

## Build Requirements

### Supported Operating Systems
- RHEL/CentOS 6/7 (or compatible)
- Windows 7/8.1/10 
 
### Dependencies
* Installation of Houdini 16.5 or later (see https://sidefx.com/download)
* Optional installation and license of CityEngine (2017.1 or later) to author rule packages 

### Toolchain & Compiler
* [cmake 3.13 or later](https://cmake.org/download)
* [conan 1.11 or later](https://www.conan.io/downloads)
* Linux: GCC 6.3
* Windows: Visual Studio 2017 (MSVC 14.1)

### Dependencies
The bootstrap steps below will take care of these additional dependencies: 
* [Esri CityEngine SDK](https://github.com/Esri/esri-cityengine-sdk)
* SideFX Houdini HDK
* Boost (only for Houdini older than 17.0)

## Build Instructions

Default is Houdini 17.5. See below how to build for different Houdini versions.

### Bootstrap

The below steps will populate your local Conan repository with dependencies for the Palladio build system. You only need to work through this section once (or if you want to upgrade one of the dependencies).

#### Linux
1. Checkout Palladio: `git clone git@github.com:esri/palladio.git && cd palladio`
1. Download CityEngine SDK: `conan create -pr conan/profiles/linux-gcc63 conan/cesdk cesdk/2.0.5403@esri-rd-zurich/stable`
1. Extract and package the HDK from your local Houdini 17 installation (adjust Z to your Houdini version): `conan create -pr conan/profiles/linux-gcc63 conan/houdini houdini/17.5.Z@sidefx/stable` (Note: use the option `-e HOUDINI_INSTALL=/path/to/your/hfs17.5.Z`, if Houdini is not installed at the standard location, e.g. at `/opt/hfs17.5.Z` for Linux).

#### Windows
1. Checkout Palladio: `git clone git@github.com:esri/palladio.git`
1. Open a Windows command shell and `cd` to the Palladio git repository
1. Download CityEngine SDK: `conan create -pr conan/profiles/windows-v141 conan/cesdk cesdk/2.0.5403@esri-rd-zurich/stable`
1. Extract and package the HDK from your local Houdini installation (adjust Z to your Houdini version): `conan create -pr conan/profiles/windows-v141 conan/houdini houdini/17.5.Z@sidefx/stable` (Note: use the option `-e HOUDINI_INSTALL=C:/path/to/your/houdini/installation`, if Houdini is not installed at the standard location for Windows).

### Building Palladio

Note: to e.g. build for Houdini 16.5, add cmake argument `-DPLD_HOUDINI_VERSION=16.5`.

#### Linux
1. Ensure GCC 6.3 is active.
1. `cd` into your Palladio git repository
1. ```mkdir -p build/release && cd build/release```
1. ```cmake -DCMAKE_BUILD_TYPE=Release ../../src```
1. ```make install``` (the plugin will be installed into your ~/houdini17.5/dso directory)

#### Windows
1. Open a MSVC 14.1 x64 shell (Visual Studio 2017) and `cd` to the Palladio git repository
1. ```mkdir build/release```
1. ```cd build/release```
1. ```cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ../../src```
1. ```nmake install``` (the plugin will be installed into your ~/houdini17.5/dso directory)

### Running Palladio
See [Quick Start](usage.md) how to launch Houdini with Palladio.

### Building and Running Unit Tests

#### Linux
1. `cd` into your Palladio git repository
1. ```mkdir -p build/relTest && cd build/relTest```
1. ```cmake -DCMAKE_BUILD_TYPE=Release -DPLD_TEST=1 ../../src```
1. ```make palladio_test```
1. run `bin/palladio_test`

#### Windows
1. Open a MSVC 14.1 x64 shell (Visual Studio 2017) and `cd` to the Palladio git repository
1. ```mkdir build/relTest```
1. ```cd build/relTest```
1. ```cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DPLD_TEST=1 ../../src```
1. ```nmake palladio_test```
1. ensure that the `bin` subdirectory of your Houdini installation is in the `PATH`
1. run `bin\palladio_test`


## Environment Variables

- `CITYENGINE_LOG_LEVEL`: controls global (minimal) log level for all assign and generate nodes. Valid values are "debug", "info", "warning", "error", "fatal"
- `HOUDINI_DSO_ERROR`: useful to debug loading issues, see http://www.sidefx.com/docs/houdini/ref/env

