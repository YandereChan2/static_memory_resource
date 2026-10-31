# Yc::mmr::memory_resource 与相关设施

在文件 `"mmr.h"` 定义

```C++
template<class T>
struct memory_resource_traits;

template<class T>
concept memory_resource = /* see below */;

template<class T>
struct is_memory_resource;

template<class T>
inline constexpr bool is_memory_resource_v =
    is_memory_resource<T>::value;

template<class T, memory_resource Res,
    /* memory-resource-pointer */ Ptr = nullptr>
class allocator;

struct new_delete_resource;
struct null_memory_resource;

template<
    auto Buffer = nullptr,
    std::size_t Length = 0,
    memory_resource Upstream = new_delete_resource,
    /* memory-resource-pointer */ UpstreamPtr = nullptr>
class monotonic_buffer_resource;

template<
    std::size_t ChunkSize = 4096,
    memory_resource Upstream = new_delete_resource,
    /* memory-resource-pointer */ UpstreamPtr = nullptr>
class lifo_pool_resource;
```

这组设施描述内存资源，并在此之上提供标准分配器和两种分配策略。中心的约束是 `Yc::mmr::memory_resource`：一个类型只要能被平等比较，并同时提供带对齐和不带对齐的分配、释放接口，就满足该约束。

整套设施围绕“能在编译期确定资源位置时，就不在运行时保存指针”这一原则设计：

- 资源的 `is_always_equal::value` 为真（默认对应无状态/空类资源），或指向资源的指针可以在模板实参中给出时，`allocator` 是空类，并且可以直接默认构造。
- 资源无法静态确定时，`allocator` 保存一个资源指针，并通过构造函数传入该指针。
- `monotonic_buffer_resource` 先使用一块初始缓冲区；缓冲区耗尽后向上游资源申请新块，并在自身析构时归还这些块。也可以调用 `release()` 提前归还所有上游块，并把分配位置重置到初始缓冲区开头。
- `lifo_pool_resource` 用可配置大小的 chunk 组成最新块优先的池；分配优先使用最新 chunk 的剩余空间，释放时优先回收最新 chunk 的尾部，最新 chunk 完全空出后归还上游。

所有设施均位于命名空间 `Yc::mmr` 中，不向 `std` 添加任何声明。`mmr` 是 Monomorphic Memory Resource 的缩写。

## 类型一览

| 设施 | 说明 |
|:-|:-|
| [`memory_resource_traits`](#memory_resource_traits) | 资源的关联指针类型与无状态性 traits |
| [`memory_resource`](#memory_resource) | 内存资源概念 |
| [`is_memory_resource`](#is_memory_resource) | 概念的布尔 traits |
| [`allocator`](allocator.md) | 适配内存资源的标准分配器 |
| [`new_delete_resource`](builtin_resources.md#new_delete_resource) | 使用全局 `operator new` / `operator delete` 的无状态资源 |
| [`null_memory_resource`](builtin_resources.md#null_memory_resource) | 分配必定失败的无状态资源 |
| [`monotonic_buffer_resource`](monotonic_buffer_resource.md) | 带初始缓冲区和上游回退的单调资源 |
| [`lifo_pool_resource`](lifo_pool_resource.md) | 面向近似 LIFO 模式的可配置 chunk 池，默认 chunk 为 4096 字节 |

## memory_resource_traits

```C++
template<class T>
struct memory_resource_traits;
```

`memory_resource_traits<T>` 描述资源类型 `T` 的指针类型和无状态性。

### 成员类型

| 成员类型 | 定义 |
|:-|:-|
| `void_pointer` | 若 `T::void_pointer` 存在，则为 `T::void_pointer`；否则为 `void*`。资源的 `allocate` 返回这个类型，`deallocate` 接受这个类型。 |
| `memory_resource_pointer` | `std::pointer_traits<void_pointer>` 重绑定到 `T` 后得到的指针类型，即指向资源对象本身的指针。 |
| `is_always_equal` | 若 `T::is_always_equal` 存在，则直接使用该嵌套类型；否则为 `std::bool_constant<std::is_empty_v<T>>`。 |

`is_always_equal` 是一个带 `::value` 的布尔常量类型。它表示“资源的任意两个实例是否可以视为等价，因此使用它的分配器不需要保存资源指针”。默认规则是：空类资源视为无状态。

`memory_resource_traits` 不提供 `pointer` 成员。`T` 是资源类型，没有 `value_type`，因此“元素指针”只在具体使用资源的设施中定义，例如 [`allocator`](allocator.md)。

`memory_resource_traits` 不支持特化；对任何类型特化 `memory_resource_traits` 都会产生未定义行为。资源类型应通过自身的嵌套类型表达 `void_pointer` 和 `is_always_equal`。

## memory_resource

```C++
template<class T>
concept memory_resource =
    std::equality_comparable<T> &&
    requires(
        T t,
        typename memory_resource_traits<T>::void_pointer p)
    {
        { t.allocate((std::size_t)0) }
            -> std::same_as<typename memory_resource_traits<T>::void_pointer>;

        { t.allocate((std::size_t)0, (std::size_t)8) }
            -> std::same_as<typename memory_resource_traits<T>::void_pointer>;

        t.deallocate(p, (std::size_t)0);
        t.deallocate(p, (std::size_t)0, (std::size_t)8);
    };
```

概念要求：

- `T` 满足 `std::equality_comparable`。
- 能以只带大小的形式调用 `allocate`，且返回类型**精确等于** `memory_resource_traits<T>::void_pointer`；仅可隐式转换不算。
- 能以大小加对齐的形式调用 `allocate`，返回类型同上。
- 能以 `void_pointer` 加大小调用 `deallocate`。
- 能以 `void_pointer` 加大小和对齐调用 `deallocate`。

概念使用 `(std::size_t)8` 作为对齐实参做检查。它不要求：

- `allocate` 或 `deallocate` 是 `noexcept`；
- 资源是空类或可默认构造；
- 分配失败一定抛出 `std::bad_alloc`；
- 释放必须立即归还内存。

这些语义由具体资源规定。

标准库的 `std::pmr::memory_resource` 也满足该概念；它是一个有状态的多态资源，因此需要运行时实例，不能靠空类优化省掉指针。

## is_memory_resource

```C++
template<class T>
struct is_memory_resource
    : std::bool_constant<memory_resource<T>> {};

template<class T>
inline constexpr bool is_memory_resource_v =
    is_memory_resource<T>::value;
```

`is_memory_resource_v<T>` 是 `memory_resource<T>` 的布尔形式，便于在 `if constexpr`、`static_assert` 和其他 traits 中使用。

## 占位模板参数

`allocator` 的第三个模板参数、`monotonic_buffer_resource` 的第四个模板参数，以及 `lifo_pool_resource` 的 `UpstreamPtr` 模板参数，在本文档中写作：

```C++
/* memory-resource-pointer */
```

它表示“可选的、指向资源对象的编译期指针”。具体含义如下（这些 `/* ... */` 只是文档占位，不是实际代码）：

- 这几个位置都是非类型模板参数；实参是资源地址，而不是类型。
- 对于 `allocator<T, Res, Ptr>`，它指向 `Res` 的实例。
- 对于 `monotonic_buffer_resource<... UpstreamPtr>` 和 `lifo_pool_resource<... UpstreamPtr>`，它指向 `Upstream` 的实例。
- 它的实际类型是 `memory_resource_traits<Res>::memory_resource_pointer`（或 `memory_resource_traits<Upstream>::memory_resource_pointer`）。
- 如果该类型不能作为非类型模板参数，或者不能由 `nullptr` 隐式初始化，那么该位置会退化为一个只能表示空值的占位类型。
- 默认值为 `nullptr`。非空表示资源的地址在编译期已经确定，此时使用该资源的设施不需要保存运行时指针；为空时，是否还需要运行时指针由资源的 `is_always_equal` 和具体构造函数决定。

作为模板实参的资源地址必须指向具有静态存储期的对象；局部自动变量不能作为这种编译期指针。

当占位类型退化时，不要依赖它的具体类型，也不要把运行时资源地址作为该模板实参。运行时资源应当通过构造函数传入。

## 指针模型与空类优化

资源通过 `memory_resource_traits<Res>::void_pointer` 暴露自己的分配指针类型。分配器再把该类型重绑定到元素类型：

```C++
using allocator_pointer =
    typename std::pointer_traits<
        typename memory_resource_traits<Res>::void_pointer>::template rebind<T>;
```

因此：

- 默认 `void*` 资源得到普通的 `T*` 分配器指针。
- 自定义 fancy `void_pointer` 资源会得到同族的缀饰指针（fancy pointer）。

使用缀饰指针（fancy pointer）时，资源返回的指针必须能通过 `std::to_address` 取得地址，并且能在资源 `void_pointer`、分配器指针和目标元素指针之间完成构造或转换；否则相应分配器或资源在实例化时无法工作。

几个设施的空类性质如下：

| 设施 | 空类条件 |
|:-|:-|
| [`allocator`](allocator.md) | 编译期资源指针非空，或资源 `is_always_equal::value` 为真 |
| [`new_delete_resource`](builtin_resources.md#new_delete_resource) | 始终为空类 |
| [`null_memory_resource`](builtin_resources.md#null_memory_resource) | 始终为空类 |
| [`monotonic_buffer_resource`](monotonic_buffer_resource.md) | 不适用；它必须保存分配状态 |
| [`lifo_pool_resource`](lifo_pool_resource.md) | 不适用；它保存分配状态 |

`allocator` 为空类时可以直接默认构造，且所有同类型实例相等；非空时不能默认构造，必须传入资源指针。

## 快速开始

```C++
#include <memory>
#include <type_traits>
#include "mmr.h"

using allocator_type =
    Yc::mmr::allocator<int, Yc::mmr::new_delete_resource>;

static_assert(std::is_empty_v<allocator_type>);

int main()
{
    allocator_type alloc;
    int* p = alloc.allocate(1);
    std::construct_at(p, 1);
    std::destroy_at(p);
    alloc.deallocate(p, 1);
}
```

有状态资源需要把资源指针传给分配器：

```C++
MyResource resource;
Yc::mmr::allocator<int, MyResource> alloc{&resource};
```

如果资源实例具有静态存储期，也可以把地址直接作为模板实参：

```C++
inline MyResource resource;

using allocator_type =
    Yc::mmr::allocator<int, MyResource, &resource>;
```

此时分配器为空类，不保存资源指针。

单调缓冲区资源的基本用法：

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

`buffer_address` 之所以声明为 `void*`，是因为静态缓冲区地址的类型必须与上游资源的 `void_pointer` 精确一致；默认上游是 `new_delete_resource`，其 `void_pointer` 为 `void*`。

近似 LIFO 池的基本用法：

```C++
#include "mmr.h"

Yc::mmr::lifo_pool_resource<> pool;
void* a = pool.allocate(64);
void* b = pool.allocate(32);

pool.deallocate(b, 32);
pool.deallocate(a, 64);
```

池按 4096 字节 chunk 向上游申请内存；最新 chunk 完全空出后会归还上游。

## 细节与约束

- 该设施是 header-only 的，包含 `"mmr.h"` 即可使用。
- 该头文件要求 C++23。
- `allocator` 遵守标准分配器的基本要求，可与 `std::allocator_traits` 和标准容器配合。
- `allocator` 的 `operator==` 比较的是**保存的资源指针指向的资源对象**，不是指针地址；空类分配器恒等。
- `monotonic_buffer_resource` 的 `operator==` 比较对象身份；它是不可复制、不可移动的。
- `lifo_pool_resource` 的 `operator==` 比较对象身份；它是不可复制、不可移动的。
- `lifo_pool_resource` 不会立即复用非顶端释放的内存；它优先回收最新 chunk 的尾部空间。
- 这些设施不提供线程同步；多个线程同时使用同一个资源需要外部同步。
- 资源分配出的对象生命周期由调用者管理；分配器只负责原始内存。
- 资源接口中的大小以字节为单位；`allocator::allocate(n)` 的 `n` 是元素个数，字节数按 `n * sizeof(T)` 计算。
- 对齐必须是有效的 2 的幂。

## 相关文档

| 文档 | 内容 |
|:-|:-|
| [`allocator`](allocator.md) | 标准分配器适配、空类条件、比较语义和 fancy pointer |
| [内置资源](builtin_resources.md) | `new_delete_resource` 与 `null_memory_resource` |
| [`monotonic_buffer_resource`](monotonic_buffer_resource.md) | 静态/动态初始缓冲区、上游资源和析构释放 |
| [`lifo_pool_resource`](lifo_pool_resource.md) | 可配置 chunk 大小、最新块优先、尾部回收和直接大块 |
