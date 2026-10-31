# Yc::mmr::lifo_pool_resource

```C++
template<
    std::size_t ChunkSize = 4096,
    memory_resource Upstream = new_delete_resource,
    /* memory-resource-pointer */ UpstreamPtr = nullptr>
class lifo_pool_resource;
```

`Yc::mmr::lifo_pool_resource` 面向近似 LIFO 的分配/释放模式：对象通常成批或嵌套地创建，释放顺序大体与创建顺序相反，但允许少量乱序。

它适合：

- 树、解析器、递归算法等具有明显嵌套生命周期的场景；
- 一次分配多个对象，随后大体逆序释放；
- 能接受“非顶端释放的内存不会立即复用”。

它不适合：

- 每次分配后立刻释放单个对象；
- 严格按任意顺序释放，并要求所有空闲内存立即复用；
- 对峰值内存非常敏感的场景。

## 模板参数

| 参数 | 默认值 | 说明 |
|:-|:-|:-|
| `ChunkSize` | `4096` | 单个 chunk 的字节数；必须是大于等于 256 的 2 的幂。chunk 的对齐也等于 `ChunkSize` |
| `Upstream` | `new_delete_resource` | chunk 和超大对象的来源 |
| `UpstreamPtr` | `nullptr` | 可选的、指向 `Upstream` 实例的编译期指针；含义见 [占位模板参数](mmr.md#占位模板参数) |

所有内存都从 `Upstream` 申请，没有静态或动态的初始缓冲区。

## 成员类型与常量

| 成员 | 定义 |
|:-|:-|
| `void_pointer` | `memory_resource_traits<Upstream>::void_pointer` |
| `upstream_type` | `Upstream` |
| `upstream_resource_pointer` | `memory_resource_traits<Upstream>::memory_resource_pointer` |
| `size_type` | `std::size_t` |
| `difference_type` | `std::ptrdiff_t` |
| `chunk_size` | `ChunkSize` |
| `chunk_alignment` | `ChunkSize` |
| `default_alignment` | `alignof(std::max_align_t)` |

上游必须能够提供 `ChunkSize` 字节对齐的 `ChunkSize` 字节分配。默认的 `new_delete_resource` 会自动使用对齐的 `operator new`。

## 构造与析构

```C++
constexpr lifo_pool_resource() noexcept
    requires /* 上游地址在编译期确定或上游无状态 */;

explicit lifo_pool_resource(upstream_resource_pointer upstream);
explicit lifo_pool_resource(Upstream& upstream);
```

- 上游无状态，或 `UpstreamPtr` 非空时，可以默认构造；
- 有状态且没有编译期上游指针时，必须传入上游指针或引用；
- 构造时不申请 chunk，第一次分配时才申请；
- 资源不可复制、不可移动；
- 析构时会归还所有 chunk 和所有仍然存活的直接大块；
- 没有单独的提前释放接口。

## allocate

```C++
[[nodiscard]] void_pointer allocate(
    size_type bytes,
    size_type alignment = alignof(std::max_align_t));
```

分配会返回至少满足 `alignment` 的内存。基本行为：

1. 优先使用最新 chunk 的未使用尾部；
2. 最新 chunk 放不下时，申请一个新的 `ChunkSize` 字节 chunk；
3. 不会回头使用旧 chunk 的剩余空间；
4. 如果请求大到无法放进一个全新的 chunk，则直接从上游分配独立大块。

由此产生的使用约束：

- 旧 chunk 只保留已经存在的对象，不会接收新的分配；
- 在较早位置释放出来的内存不会立即复用，只有等到它重新成为最新 chunk 的尾部区域时才可能被再次使用；
- 过对齐请求可能需要额外的对齐填充；如果填充后连一个全新 chunk 都放不下，则走直接大块路径；
- 直接大块在 `deallocate` 时立即归还上游。

## deallocate

```C++
void deallocate(
    void_pointer p,
    size_type bytes,
    size_type alignment = alignof(std::max_align_t)) noexcept;
```

释放时应传入与分配时相同的 `bytes` 和 `alignment`；标准分配器和容器会自动这样做。

行为：

- 释放相邻的空闲区域会自动合并；
- 如果释放后一整段空闲区域到达最新 chunk 的尾部，资源会回收这段尾部空间，供后续分配使用；
- 如果最新 chunk 因此完全空出，会归还上游，并把更早的 chunk 设为最新；更早的 chunk 如果也为空，会继续归还；
- 旧 chunk 即使已经空出，也要等它成为最新 chunk 后才会归还上游；
- 非顶端释放的内存不会立即进入可分配池；
- 直接大块直接归还上游，不经过 chunk。

换句话说，资源会优先回收“最新 chunk 的尾部”，但不会为了一个中间空洞去搜索旧 chunk。

## upstream_resource

```C++
[[nodiscard]] upstream_resource_pointer upstream_resource() const noexcept;
```

返回使用的上游资源指针：

- `UpstreamPtr` 非空时返回该编译期指针；
- 有状态上游且没有编译期指针时，返回保存的运行时指针；
- 无状态上游返回默认上游实例的指针。

该函数只观察，不转移所有权。

## 使用示例

```C++
#include <cassert>
#include "mmr.h"

int main()
{
    Yc::mmr::lifo_pool_resource<> pool;

    void* a = pool.allocate(64);
    void* b = pool.allocate(32);

    pool.deallocate(b, 32);
    pool.deallocate(a, 64);

    void* c = pool.allocate(128);
    assert(c != nullptr);
    pool.deallocate(c, 128);
}
```

有状态上游：

```C++
MyUpstream upstream;
Yc::mmr::lifo_pool_resource<4096, MyUpstream> pool{upstream};
```

编译期上游：

```C++
inline MyUpstream upstream;
using pool_type = Yc::mmr::lifo_pool_resource<4096, MyUpstream, &upstream>;
pool_type pool; // 默认构造，不需要保存运行时上游指针
```

## 使用建议

### 选择合适的 ChunkSize

默认的 4096 是通用选择。`ChunkSize` 决定内存粒度：

- 较大的 chunk 减少向上游申请/归还的次数，但可能提高峰值占用；
- 较小的 chunk 内存粒度更细，但会显著增加 chunk 申请次数；
- chunk 更大并不自动意味着总性能更好，应该按实际批处理深度和释放顺序选择。

对于 64 字节左右的对象：

- 4096 大约能放 50 个；
- 8192 大约能放 101 个；
- 如果一批固定为 64 个对象，8192 可以刚好放进一个 chunk，而 4096 需要两个。

### 避免极浅的 LIFO

如果每次只分配一个对象并立即释放，最新 chunk 会反复变空并归还上游，资源会不断申请/释放 chunk，这通常比默认分配器更慢。此时应直接使用默认分配器，或改用能保留空 chunk 的池策略。

### 峰值内存

池会为每个分配保存元数据，并可能保留尚未归还的 chunk，因此峰值内存会高于默认分配器。大量对象同时存活时，应把这一点计入预算。

### 乱序释放较多时

如果释放顺序比较随机、同时存活的对象很多，较大的 chunk 通常能减少上游调用；但非顶端空洞仍然不会立即复用。若需要严格的任意顺序回收，不应使用这个资源。

## 注意事项

- 只有最新 chunk 变空时才会归还上游；旧 chunk 的空闲空间要等它成为最新 chunk。
- 直接大块不受 chunk 限制，`deallocate` 时立即归还上游。
- 析构函数会归还所有 chunk 和直接大块；没有单独的提前释放接口。
- 资源不提供线程同步；多个线程同时使用同一个资源需要外部同步。
- 上游必须支持 `ChunkSize` 字节对齐。
- 资源是非空类；用 `allocator` 适配它时需要保存资源指针，或通过构造函数传入资源引用。
- `deallocate` 的指针必须来自同一个资源的 `allocate`，并且大小和对齐应与分配时一致。

## 相关文档

| 文档 | 内容 |
|:-|:-|
| [设施总览](mmr.md) | traits、概念、指针模型和模板形参 |
| [`allocator`](allocator.md) | 标准分配器适配和 fancy pointer |
| [内置资源](builtin_resources.md) | `new_delete_resource` 与 `null_memory_resource` |
| [`monotonic_buffer_resource`](monotonic_buffer_resource.md) | 单调资源、初始缓冲区和 `release()` |
