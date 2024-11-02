# C/CPP

Installs [build-essentials](https://packages.ubuntu.com/jammy/build-essential) and optionally [CMake](https://cmake.org), [Boost](https://www.boost.org) and [fmt](https://fmt.dev/latest/index.html).

`build-essential` is what is called a meta-package. It in itself does not install anything. Instead, it is a link to several other packages that will be installed as dependencies.
In the case of the build-essential meta-package, it will install everything required for compiling basic software written in C and C++.

`Boost` provides free peer-reviewed portable C++ source libraries.

`{fmt}` is an open-source formatting library providing a fast and safe alternative to C stdio and C++ iostreams.

```
    "features": {
        "ghcr.io/jeffersonjhunt/features/cpp:1": {}	
    }
```

## Options

| Options      | Description    | Type    | Default Value |
| ------------ | -------------- | ------- | ------------- |
| installBoost | Install Boost? | boolean | true          |
| installCMake | Install CMake? | boolean | true          |
| installFmt   | Install fmt?   | boolean | true          |

## OS Support

This Feature should work on recent versions of Debian/Ubuntu, RedHat Enterprise Linux, Fedora, RockyLinux, and Alpine Linux.
