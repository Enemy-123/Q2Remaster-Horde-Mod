Package: fmt:x64-linux@11.0.2#1

**Host Environment**

- Host: x64-linux
- Compiler: GNU 11.4.0
-    vcpkg-tool version: 2025-07-21-d4b65a2b83ae6c3526acd1c6f3b51aff2a884533
    vcpkg-scripts version: 3bdaa9b420 2025-07-27 (33 hours ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
Downloading https://github.com/fmtlib/fmt/archive/11.0.2.tar.gz -> fmtlib-fmt-11.0.2.tar.gz
warning: Problem : HTTP error. Will retry in 1 seconds. 3 retries left.
warning: Problem : HTTP error. Will retry in 2 seconds. 2 retries left.
warning: Problem : HTTP error. Will retry in 4 seconds. 1 retries left.
error: curl: (22) The requested URL returned error: 503
note: If you are using a proxy, please ensure your proxy settings are correct.
Possible causes are:
1. You are actually using an HTTP proxy, but setting HTTPS_PROXY variable to `https//address:port`.
This is not correct, because `https://` prefix claims the proxy is an HTTPS proxy, while your proxy (v2ray, shadowsocksr, etc...) is an HTTP proxy.
Try setting `http://address:port` to both HTTP_PROXY and HTTPS_PROXY instead.
2. If you are using Windows, vcpkg will automatically use your Windows IE Proxy Settings set by your proxy software. See: https://github.com/microsoft/vcpkg-tool/pull/77
The value set by your proxy might be wrong, or have same `https://` prefix issue.
3. Your proxy's remote server is our of service.
If you believe this is not a temporary download server failure and vcpkg needs to be changed to download this file from a different location, please submit an issue to https://github.com/Microsoft/vcpkg/issues
CMake Error at scripts/cmake/vcpkg_download_distfile.cmake:136 (message):
  Download failed, halting portfile.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_from_github.cmake:120 (vcpkg_download_distfile)
  buildtrees/versioning_/versions/fmt/07a73a7565e5de9eb42e90c16c133bdfdfebbcda/portfile.cmake:1 (vcpkg_from_github)
  scripts/ports.cmake:206 (include)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "q2-game-dll",
  "version": "2022",
  "builtin-baseline": "dd3097e305afa53f7b4312371f62058d2e665320",
  "dependencies": [
    {
      "name": "fmt"
    },
    {
      "name": "jsoncpp"
    }
  ]
}

```
</details>
