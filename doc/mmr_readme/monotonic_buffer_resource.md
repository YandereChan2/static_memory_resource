# Yc::mmr::monotonic_buffer_resource

在文件 `"mmr.h"` 定义

```C++
template<
    auto Buffer = nullptr,
    std::size_t Length = 0,
    memory_resource Upstream = new_delete_resource,
    /* memory-resource-pointer */ UpstreamPtr = nullptr>
class monotonic_buffer_resource;
```

`Yc::mmr::monotonic_buffer_resource` 是一个单调内存资源：它从一块初始缓冲区中按递增地址分配内存；当剩余空间不足时，向上游资源申请新块。对单独一块内存的 `deallocate` 不执行任何操作。所有从上游申请的块会在资源析构时统一归还，也可以调用 [`release()`](#release) 提前归还并重新使用初始缓冲区。

初始缓冲区有两种来源：

- 编译期静态缓冲区：由模板参数 `Buffer` 和 `Length` 指定。
- 运行时动态缓冲区：`Buffer` 为空，缓冲区指针和长度通过构造函数传入。

上游资源由类型 `Upstream` 和可选的编译期指针 `UpstreamPtr` 指定。上游指针同样遵循“能在编译期确定就不保存运行时指针”的原则。

模板参数中的 `/* memory-resource-pointer */` 含义见 [占位模板参数](mmr.md#占位模板参数)。对于本资源，它指向 `Upstream` 的实例。

## 模板参数

| | |
|-:|:-|
|`Buffer`|初始缓冲区的编译期地址，默认 `nullptr`。非空时必须指向具有静态存储期的对象、能作为非类型模板参数，且类型**精确等于** `memory_resource_traits<Upstream>::void_pointer`。为空表示缓冲区由构造函数动态传入。|
|`Length`|初始缓冲区的字节数。`Buffer` 为空时必须为 `0`；`Buffer` 非空时表示静态缓冲区的大小。|
|`Upstream`|上游内存资源类型，必须满足 [`memory_resource`](mmr.md#memory_resource)。默认 `new_delete_resource`。初始缓冲区耗尽后从这里申请内存。|
|`UpstreamPtr`|可选的、指向 `Upstream` 实例的编译期指针，默认 `nullptr`。类型和含义见[占位模板参数](mmr.md#占位模板参数)。|

`Length` 的单位是字节，不是元素个数。

## 静态缓冲区

当 `Buffer` 非空时，资源使用编译期缓冲区，并满足以下编译期约束：

```C++
static_assert(
    std::is_same_v<
        decltype(Buffer),
        typename memory_resource_traits<Upstream>::void_pointer>);
```

要求的是类型完全相同，而不是可转换。默认上游 `new_delete_resource` 的 `void_pointer` 是 `void*`，因此默认配置下需要传入一个 `void*` 类型的编译期值：

```C++
alignas(64) inline std::byte buffer[1024];
inline constexpr void* buffer_address = buffer;
```

`Buffer` 非空时，`Length` 可以为零；这表示有一个静态缓冲区地址，但没有可用的初始空间，第一次分配就会转向上游。

`Buffer` 为空时，`Length` 必须为零：

```C++
// Buffer 为空 => Length == 0
```

也就是说，不存在“缓冲区地址为空、但给出了静态长度”的合法配置。`Buffer` 非空时，`Length` 可以为任意值，包括 `0`。

## 动态缓冲区

当 `Buffer` 为空时，初始缓冲区必须通过构造函数传入。缓冲区指针的类型是资源的 `void_pointer`，长度在运行时给出：

```C++
Yc::mmr::monotonic_buffer_resource<> resource{
    buffer, buffer_size};
```

传入空指针或长度 `0` 表示没有初始缓冲区；此时所有分配都会转向上游。

## 上游资源

上游资源遵循与 `allocator` 相同的静态化规则：

- `UpstreamPtr` 非空时，上游实例在编译期确定，资源不需要保存运行时指针。
- `UpstreamPtr` 为空，但 `memory_resource_traits<Upstream>::is_always_equal::value` 为真时，资源仍不需要保存运行时指针；它使用一个默认的上游实例。
- 否则，资源必须通过构造函数获得上游资源指针或引用。

上游资源可以是有状态或无状态的，也可以是 `std::pmr::memory_resource` 这样的多态资源类型。

## 成员类型

|成员类型|定义|
|-:|:-|
|`void_pointer`|`memory_resource_traits<Upstream>::void_pointer`|
|`upstream_type`|`Upstream`|
|`upstream_resource_pointer`|`memory_resource_traits<Upstream>::memory_resource_pointer`，即指向上游资源对象的指针|
|`size_type`|`std::size_t`|
|`difference_type`|`std::ptrdiff_t`|

资源的 `void_pointer` 与上游一致。默认上游得到 `void*`；带缀饰指针（fancy pointer）的上游得到同族指针。

## 构造与析构

记“静态上游”为：

```C++
UpstreamPtr 非空 ||
memory_resource_traits<Upstream>::is_always_equal::value
```

构造函数按初始缓冲区和上游的静态性分为四种组合：

| 初始缓冲区 | 上游 | 构造函数 |
|:-|:-|:-|
| 静态 | 静态 | `monotonic_buffer_resource()` |
| 静态 | 运行时 | `explicit monotonic_buffer_resource(upstream_resource_pointer)` 或 `explicit monotonic_buffer_resource(Upstream&)` |
| 动态 | 静态 | `monotonic_buffer_resource(void_pointer buffer, size_type length)` |
| 动态 | 运行时 | `monotonic_buffer_resource(void_pointer buffer, size_type length, upstream_resource_pointer)` 或 `monotonic_buffer_resource(void_pointer buffer, size_type length, Upstream&)` |

其中 `upstream_resource_pointer` 是 `memory_resource_traits<Upstream>::memory_resource_pointer`。

- 静态缓冲区模式没有“传入缓冲区”的构造函数。
- 动态缓冲区模式没有默认构造函数；必须先提供缓冲区（可以为空）和长度。
- 静态上游模式下没有上游参数。
- 运行时上游模式下必须提供上游指针或引用。

资源不可复制、不可移动。复制或移动它会破坏“所有上游块只被归还一次”的约束。

析构函数会通过上游资源归还所有由它申请的新块。也可以调用 `release()` 提前完成这次归还，并把分配位置重置到初始缓冲区开头。析构函数和 `release()` 都不会：

- 释放静态缓冲区；
- 释放构造函数传入的动态缓冲区；
- 销毁上游资源对象。

动态缓冲区的生命周期必须覆盖整个资源；上游资源也一样。否则资源析构或调用 `release()` 时访问它们的行为未定义。

## 成员函数

| | |
|:-|:-|
|`allocate`|从初始缓冲区或上游资源分配原始内存 <br>（公开成员函数）|
|`deallocate`|不执行任何操作 <br>（公开成员函数）|
|`release`|归还所有上游块，并重置分配位置到初始缓冲区开头 <br>（公开成员函数）|
|`upstream_resource`|返回上游资源指针 <br>（公开成员函数）|
|`operator==`|比较两个资源的对象身份 <br>（公开成员函数）|

### allocate

```C++
[[nodiscard]] void_pointer allocate(
    size_type bytes,
    size_type alignment = alignof(std::max_align_t));
```

从当前缓冲区位置开始，寻找满足 `alignment` 的地址：

- 如果初始缓冲区或当前上游块还有足够空间，则直接返回其中的地址，下一次分配从新位置继续；
- 否则向上游申请一个新块，并从新块中返回地址；
- 如果上游分配失败，异常原样传播；如果块大小计算无法表示，也会抛出 `std::bad_alloc`。

`alignment` 是 2 的幂；传入 `0` 时按 `1` 处理。

返回的指针在资源析构或调用 `release()` 之前一直有效，且不会被单独回收。资源不限制分配次数，除非上游资源耗尽或抛出异常。

### deallocate

```C++
void deallocate(
    void_pointer p,
    size_type bytes,
    size_type alignment = alignof(std::max_align_t)) noexcept;
```

单调资源不执行单次释放；该函数什么也不做。所有从上游申请的内存只在资源析构或调用 `release()` 时统一归还；初始缓冲区不由资源释放。

### release

```C++
void release() noexcept;
```

归还所有由本资源从上游申请的块，并把分配位置重置到初始缓冲区的开头。因此：

- 静态缓冲区和动态缓冲区都可以在 `release()` 之后从开头重新使用；
- 如果没有初始缓冲区，`release()` 之后的所有分配仍然直接转向上游；
- 初始缓冲区本身不会被释放，上游资源对象也不会被销毁；
- `release()` 可以重复调用；没有上游块时只重置分配位置。

调用 `release()` 会使此前通过 `allocate()` 获得的全部指针失效。调用者必须先结束这些内存中对象的生命周期（以及不再访问这些指针），再调用 `release()`。初始缓冲区的内容不会被清除，后续分配会从缓冲区开头重新覆盖。

### upstream_resource

```C++
[[nodiscard]] upstream_resource_pointer upstream_resource() const noexcept;
```

返回本资源使用的上游资源指针。返回类型是公开别名：

```C++
using upstream_resource_pointer =
    typename memory_resource_traits<Upstream>::memory_resource_pointer;
```

具体返回哪个指针，取决于上游的配置方式：

- `UpstreamPtr` 非空时，返回该编译期指针；
- 上游没有编译期指针、但存在运行时上游时，返回保存的运行时指针；
- 上游无状态（`is_always_equal::value` 为真）且没有编译期指针时，返回指向默认上游实例的指针。

该函数只观察，不转移所有权。返回的指针在对应上游资源的生命周期内有效。对于普通指针，可以直接与 `&upstream` 比较；对于 fancy pointer，可以用 `std::to_address` 取得底层地址后再比较。

### operator==

```C++
bool operator==(const monotonic_buffer_resource& other) const noexcept;
```

比较对象身份。两个不同的单调资源即使使用相同的静态缓冲区或相同的上游，也不相等。复制得到的对象不存在，因为资源不可复制。

`operator!=` 由 `operator==` 重写生成。

## 指针模型

资源返回的 `void_pointer` 来自上游：

```C++
using void_pointer =
    typename memory_resource_traits<Upstream>::void_pointer;
```

- 静态缓冲区的地址类型必须精确等于这个类型；
- 动态缓冲区的构造函数参数类型也是这个类型；
- 缓冲区内的地址会以同族的 `void_pointer` 表示。

使用缀饰指针（fancy pointer）时，指针族需要支持：

- 用 `std::to_address` 取得底层地址；
- 从底层地址构造回 `void_pointer`；
- 足以表示缓冲区中的偏移地址。

不满足这些条件的 fancy pointer 会在相应成员函数实例化时编译失败。

## 示例

### 静态缓冲区 + 默认上游

```C++
#include <cstddef>
#include "mmr.h"

alignas(64) inline std::byte buffer[1024];
inline constexpr void* buffer_address = buffer;

using resource_type = Yc::mmr::monotonic_buffer_resource<
    buffer_address, sizeof(buffer)>;

resource_type resource;
void* p = resource.allocate(64, 64);
```

这里 `buffer_address` 必须是 `void*`，因为默认上游的 `void_pointer` 是 `void*`。

### 动态缓冲区 + 默认上游

```C++
std::byte buffer[1024]{};

Yc::mmr::monotonic_buffer_resource<> resource{
    buffer, sizeof(buffer)};

void* p = resource.allocate(64, 64);
```

传入 `nullptr, 0` 表示没有初始缓冲区：

```C++
Yc::mmr::monotonic_buffer_resource<> resource{nullptr, 0};
```

### 有状态上游

如果上游实例具有静态存储期，可以直接把地址作为模板实参：

```C++
inline MyUpstream upstream;

using resource_type = Yc::mmr::monotonic_buffer_resource<
    buffer_address, 16, MyUpstream, &upstream>;

resource_type resource; // 上游是“静态”的，不需要构造参数
```

如果上游只在运行时可用：

```C++
MyUpstream upstream;

using resource_type = Yc::mmr::monotonic_buffer_resource<
    buffer_address, 16, MyUpstream>;

resource_type resource{upstream};
```

动态缓冲区加运行时上游：

```C++
using resource_type = Yc::mmr::monotonic_buffer_resource<
    nullptr, 0, MyUpstream>;

resource_type resource{buffer, sizeof(buffer), upstream};
```

### 提前释放并复用初始缓冲区

```C++
#include <cstddef>
#include "mmr.h"

alignas(64) std::byte buffer[256]{};

Yc::mmr::monotonic_buffer_resource<> resource{
    buffer, sizeof(buffer)};

void* first = resource.allocate(32, 8);

resource.release(); // 归还所有上游块；first 现在失效

void* second = resource.allocate(32, 8); // 再次从 buffer 开头分配
```

### 作为静态分配器的资源

沿用上文的 `resource_type`：

```C++
using allocator_type =
    Yc::mmr::allocator<int, resource_type>;

allocator_type alloc{&resource};
int* p = alloc.allocate(8);
alloc.deallocate(p, 8);
```

单调资源本身不是空类，因此 `allocator_type` 也不是空类，必须传入资源指针。

### 使用空上游测试耗尽

```C++
#include <cstddef>
#include <new>
#include "mmr.h"

alignas(64) inline std::byte buffer[64];
inline constexpr void* buffer_address = buffer;

using resource_type = Yc::mmr::monotonic_buffer_resource<
    buffer_address, 64, Yc::mmr::null_memory_resource>;

resource_type resource;
void* p = resource.allocate(32); // 来自初始缓冲区，成功

try
{
    (void)resource.allocate(128); // 初始缓冲区不足，转向上游
}
catch (const std::bad_alloc&)
{
    // null_memory_resource 的分配总是失败
}
```

## 注解

- 初始缓冲区和上游都属于资源的外部依赖，资源不拥有它们。
- 单次 `deallocate` 不影响后续分配；从上游申请的内存直到资源析构或调用 `release()` 才归还。
- `release()` 会归还上游块，并把分配位置重置到初始缓冲区开头；它会使此前分配的所有指针失效。
- `upstream_resource()` 返回上游资源指针；它不转移所有权，也不影响分配状态。
- 析构和 `release()` 都会归还所有上游块；如果上游释放可能抛出异常，不应该让异常离开析构函数或 `release()`。
- 对齐必须是有效的 2 的幂。默认对齐是 `alignof(std::max_align_t)`。
- 若 `UpstreamPtr` 的类型不能作为非类型模板参数，模板实参位置会退化为只能表示空值的占位类型；此时上游必须通过构造函数传入，除非 `Upstream` 无状态。
- 资源不提供同步；多线程同时使用同一个资源需要外部同步。
- 资源的 `memory_resource_traits<...>::is_always_equal::value` 为 `false`，因为它保存运行时分配状态。
- 初始缓冲区耗尽后使用上游新块；本设施不承诺新块的具体大小或增长策略。
