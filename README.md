# 说明

这是一个依赖于 Digikam 的插件，为 Digikam 添加了更加紧凑、美观的流式布局。

刚写这个软件的时候不是很清楚版本号的发行规范，因此早期版本有些问题，但是所幸今天（2021-08-10）迎来了 v0.1.0 版本，在上面的十个版本中我完成了软件的外观实现和性能优化，从部件布局一体化到部件布局分离，从单线程到读写线程分离，从双线程到多线程。

在 v0.0.8 中使用图片压缩大幅降低了大图片的内存将用，而且使用了多线程读图片大幅度提高了图片读取速度。

在 v0.0.9 版本中修复了小图片占用更多内存的问题。

v0.1.0 是一个比较简单的版本，只是修复了 CPack 的问题。

至此，此项目应该已经足够健壮了，除了一些在小问题外，日常使用应当问题不大。目前疑存的 Bug
是一个窗口停止会导致其他窗口停止加载图片，但是这个测试起来比较棘手，而且我日常使用感知不到，就暂时不管了。另外几个相关的 issues 是：

- 是否让多个窗口共用锁？
- 是否添加接口限制图片的最大读取数量？
- 是否添加自定义图片压缩的大小？
- 是否在双击图片时显示无损大图？
- <s>加载图片时会不会导致软件崩溃？这点可能需要更改 FlowView 的库（这个随机 Bug 似乎是由于 List
  是非线程安全引起的，为了效率，可能需要重写一个 List 以保证最小临界区）<s/>
- 是否需要设置图片缓存？如果是，图片缓存设置多大合适，还是说将接口提供给用户？（但是用户一般对看图片占用的内存一无所知，包括我。。。）
- 看起来似乎图片大小的计算方式有些问题，图片占用的空间要超过可显示的部分
- 图片可以根据数量将其分到不同的线程中，这样要求将生产者的接口改成一个迭代器范围，属于可优化而且不是很难的部分

2021-08-13

最近发现了 kitty 这个软件，号称是 OpenGL 渲染的终端，用起来感觉和 Konsole
确实不一样，到底那里不一样又说不出来，打开图片一对比，发现 kitty
渲染的图片发丝更加清楚。。。图片看起来也更清楚

所以如果我使用硬件渲染的话，性能会不会有所改善？图片质量会不会更高呢？

# 截图

![1.gif](screenshot/1.gif)
![缩放.gif](./screenshot/缩放.gif)
![节省内存.gif](./screenshot/节省内存.gif)

# 构建

此项目构建时依赖于以下组件（Fedora）：

- spdlog-devel
- digikam-devel
- qt5-qtbase-devel
- c++ 17
- CMake

要构建并安装，请执行以下指令：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```
