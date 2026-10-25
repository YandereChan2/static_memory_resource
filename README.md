# static_memory_resource

C++23 header-only 设施，提供：

- `Yc::memory_resource_traits`
- `Yc::static_memory_resource` 概念与 `Yc::is_static_memory_resource`
- 标准分配器 `Yc::static_allocator`
- 无状态资源 `Yc::static_new_delete_resource` 与 `Yc::static_null_memory_resource`
- 单调缓冲区资源 `Yc::static_monotonic_buffer_resource`

资源位置在编译期已知或资源无状态时，`static_allocator` 是空类；无法静态确定时，分配器保存资源指针。单调资源先使用初始缓冲区，耗尽后向上游资源申请新块，并在析构时统一归还。

## 文件

- [static_memory_resource.h](static_memory_resource.h)
- 文档：
  - [设施总览](doc/static_memory_resource_readme/static_memory_resource.md)
  - [`static_allocator`](doc/static_memory_resource_readme/static_allocator.md)
  - [内置资源](doc/static_memory_resource_readme/builtin_resources.md)
  - [`static_monotonic_buffer_resource`](doc/static_memory_resource_readme/static_monotonic_buffer_resource.md)

## 快速开始

```C++
#include <memory>
#include "static_memory_resource.h"

using allocator_type =
    Yc::static_allocator<int, Yc::static_new_delete_resource>;

int main()
{
    allocator_type alloc;
    int* p = alloc.allocate(1);
    std::construct_at(p, 1);
    std::destroy_at(p);
    alloc.deallocate(p, 1);
}
```

## 环境

- C++23
- header-only，直接包含 `static_memory_resource.h`
- 分配失败通过 `std::bad_alloc` 报告；`static_null_memory_resource` 总是抛出该异常

## 许可证

[MIT](LICENSE)
