# mmr

C++23 header-only 设施，命名空间为 `Yc::mmr`。`mmr` 表示 Monomorphic Memory Resource。

提供：

- `Yc::mmr::memory_resource_traits`
- `Yc::mmr::memory_resource` 概念与 `Yc::mmr::is_memory_resource`
- 标准分配器 `Yc::mmr::allocator`
- 无状态资源 `Yc::mmr::new_delete_resource` 与 `Yc::mmr::null_memory_resource`
- 单调缓冲区资源 `Yc::mmr::monotonic_buffer_resource`
- 面向近似 LIFO 模式的可配置 chunk 池 `Yc::mmr::lifo_pool_resource`

资源位置在编译期已知或资源无状态时，`allocator` 是空类；无法静态确定时，分配器保存资源指针。单调资源先使用初始缓冲区，耗尽后向上游资源申请新块，并在析构时统一归还。池资源优先使用最新 chunk 的剩余空间，释放时优先回收最新 chunk 的尾部；最新 chunk 完全空出后归还上游。

## 文件

- [mmr.h](mmr.h)
- 文档：
  - [设施总览](doc/mmr_readme/mmr.md)
  - [`allocator`](doc/mmr_readme/allocator.md)
  - [内置资源](doc/mmr_readme/builtin_resources.md)
  - [`monotonic_buffer_resource`](doc/mmr_readme/monotonic_buffer_resource.md)
  - [`lifo_pool_resource`](doc/mmr_readme/lifo_pool_resource.md)

## 快速开始

```C++
#include <memory>
#include "mmr.h"

using allocator_type =
    Yc::mmr::allocator<int, Yc::mmr::new_delete_resource>;

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
- header-only，直接包含 `mmr.h`
- 分配失败通过 `std::bad_alloc` 报告；`null_memory_resource` 总是抛出该异常

## 许可证

[MIT](LICENSE)
