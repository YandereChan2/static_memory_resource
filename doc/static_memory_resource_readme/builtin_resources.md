# Yc::static_new_delete_resource 与 Yc::static_null_memory_resource

在文件 `"static_memory_resource.h"` 定义

```C++
struct static_new_delete_resource;
struct static_null_memory_resource;
```

这两个资源都是无状态的空类，可以直接默认构造，所有同类型实例相等，并且 `memory_resource_traits<T>::is_always_equal::value` 为 `true`。它们可以作为 [`static_allocator`](static_allocator.md) 的资源，也可以作为 [`static_monotonic_buffer_resource`](static_monotonic_buffer_resource.md) 的上游资源。

## static_new_delete_resource

```C++
struct static_new_delete_resource
{
    using void_pointer = void*;

    static constexpr std::size_t default_alignment =
        __STDCPP_DEFAULT_NEW_ALIGNMENT__;

    [[nodiscard]] void* allocate(
        std::size_t bytes,
        std::size_t alignment = default_alignment);

    void deallocate(
        void* p,
        std::size_t bytes,
        std::size_t alignment = default_alignment) noexcept;

    bool operator==(const static_new_delete_resource&) const noexcept = default;
};
```

`Yc::static_new_delete_resource` 把资源接口转发到全局 `operator new` / `operator delete`。

### 分配行为

- 当 `alignment <= default_alignment` 时，调用 `::operator new(bytes)`。
- 当 `alignment > default_alignment` 时，调用 `::operator new(bytes, std::align_val_t{alignment})`。
- `default_alignment` 等于 `__STDCPP_DEFAULT_NEW_ALIGNMENT__`。
- 分配失败时由全局 `operator new` 抛出 `std::bad_alloc`。

### 释放行为

- 当 `alignment <= default_alignment` 时，调用 `::operator delete(p)`。
- 当 `alignment > default_alignment` 时，调用 `::operator delete(p, std::align_val_t{alignment})`。
- `bytes` 参数不参与释放；调用者必须传入与分配时相同的对齐。
- 释放函数为 `noexcept`。

### 示例

```C++
#include <type_traits>
#include "static_memory_resource.h"

using allocator_type =
    Yc::static_allocator<int, Yc::static_new_delete_resource>;

static_assert(std::is_empty_v<allocator_type>);

allocator_type alloc;
int* p = alloc.allocate(8);
alloc.deallocate(p, 8);
```

## static_null_memory_resource

```C++
struct static_null_memory_resource
{
    using void_pointer = void*;

    void* allocate(
        std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t));

    void deallocate(
        void* p,
        std::size_t bytes,
        std::size_t alignment = alignof(std::max_align_t)) noexcept;

    bool operator==(const static_null_memory_resource&) const noexcept = default;
};
```

`Yc::static_null_memory_resource` 是一个“永远无法分配”的资源：

- `allocate` 不论参数为何，都抛出 `std::bad_alloc`。
- `deallocate` 什么也不做，并且是 `noexcept`。

它适合用于：

- 测试上游资源耗尽或分配失败路径；
- 在只需要初始缓冲区、不允许回退到上游的场景中作为上游资源；
- 占位一个“没有上游”的配置。

### 示例

```C++
#include <cassert>
#include <new>
#include "static_memory_resource.h"

Yc::static_null_memory_resource resource;

bool threw = false;
try
{
    (void)resource.allocate(1);
}
catch (const std::bad_alloc&)
{
    threw = true;
}
assert(threw);

resource.deallocate(nullptr, 0);
```

## 共同性质

| 性质 | `static_new_delete_resource` | `static_null_memory_resource` |
|:-|:-|:-|
| 是否有状态 | 否 | 否 |
| 是否为空类 | 是 | 是 |
| 是否可直接默认构造 | 是 | 是 |
| `void_pointer` | `void*` | `void*` |
| 相等比较 | 所有实例相等 | 所有实例相等 |
| `is_always_equal::value` | `true` | `true` |
| `allocate` 是否可能成功 | 是，受全局 `operator new` 约束 | 否，总是抛出 `std::bad_alloc` |
| `deallocate` | 转发到匹配的全局 `operator delete` | 不做任何事 |

## 注解

- 这两个资源自身不保存任何状态，因此使用它们的 `static_allocator` 是空类。
- `static_new_delete_resource` 的 `deallocate` 只使用调用者传入的对齐参数；大小参数被忽略。
- `static_null_memory_resource` 的 `deallocate` 可以接受任意指针和大小，但不会解引用这些参数。
- 两者都可以作为 `static_monotonic_buffer_resource` 的上游；使用空资源时，初始缓冲区耗尽后分配会抛出 `std::bad_alloc`。
