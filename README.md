**WebRTC is a free, open software project** that provides browsers and mobile
applications with Real-Time Communications (RTC) capabilities via simple APIs.
The WebRTC components have been optimized to best serve this purpose.

**Our mission:** To enable rich, high-quality RTC applications to be
developed for the browser, mobile platforms, and IoT devices, and allow them
all to communicate via a common set of protocols.

The WebRTC initiative is a project supported by Google, Mozilla and Opera,
amongst others.

### Development

See http://www.webrtc.org/native-code/development for instructions on how to get
started developing with the native code.

### More info

 * Official web site: http://www.webrtc.org
 * Master source code repo: https://webrtc.googlesource.com/src
 * Samples and reference apps: https://github.com/webrtc
 * Mailing list: http://groups.google.com/group/discuss-webrtc
 * Continuous build: http://build.chromium.org/p/client.webrtc
 * [Coding style guide](style-guide.md)
 * [Code of conduct](CODE_OF_CONDUCT.md)

# 编译

按 https://webrtc.org/native-code/development/prerequisite-sw/ 的指引安装编译工具

新建 `.gclient` 文件，内容如下：

```
solutions = [
  {
    "url": "https://github.com/qbox/webrtc.git",
    "managed": False,
    "name": "src",
    "deps_file": "DEPS",
    "custom_deps": {},
  },
]
```

获取源码：

```
gclient sync
```

编译 x264/fdkaac：

```
cd src/third_party/x264
./conf.sh
make install

cd src/third_party/fdkaac
./conf.sh
make install
```
