# Yc::mmr::allocator

在文件 `"mmr.h"` 定义

```C++
template<class T, memory_resource Res,
    /* memory-resource-pointer */ Ptr = nullptr>
class allocator;
```

`Yc::mmr::allocator` 是把 `Yc::mmr::memory_resource` 适配为标准分配器的类模板。其元素类型为 `T`，所有原始内存都通过资源 `Res` 分配和释放。

分配器的元素指针不是固定为 `T*`，而是从资源的 `void_pointer` 重绑定得到：

```C++
using pointer =
    typename std::pointer_traits<
        typename memory_resource_traits<Res>::void_pointer>::template rebind<T>;
```

因此默认的 `void*` 资源得到普通 `T*`，fancy `void_pointer` 资源得到同族 fancy 指针。

模板参数中的 `/* memory-resource-pointer */` 表示可选的、指向 `Res` 实例的编译期指针。其完整含义见 [占位模板参数](mmr.md#占位模板参数)。简言之：

- 它非空时，分配器不需要保存资源指针，并且是空类；
- 它为空时，如果资源本身无状态（`is_always_equal::value` 为真），分配器仍为空类；否则分配器保存运行时资源指针，必须通过构造函数传入；
- 如果该指针类型不能作为非类型模板参数，模板实参位置退化为只能表示空值的占位类型。

## 模板参数

| | |
|-:|:-|
|`T`|元素类型。分配器为 `T` 分配原始、未构造的内存。|
|`Res`|内存资源类型，必须满足 [`memory_resource`](mmr.md#memory_resource)。|
|`Ptr`|可选的编译期资源指针，默认 `nullptr`。类型和含义见[占位模板参数](mmr.md#占位模板参数)。|

## 成员类型

|成员类型|定义|
|-:|:-|
|`value_type`|`T`|
|`pointer`|`typename std::pointer_traits<typename memory_resource_traits<Res>::void_pointer>::template rebind<T>`|
|`const_pointer`|`typename std::pointer_traits<typename memory_resource_traits<Res>::void_pointer>::template rebind<const T>`|
|`void_pointer`|`typename memory_resource_traits<Res>::void_pointer`|
|`const_void_pointer`|`typename std::pointer_traits<typename memory_resource_traits<Res>::void_pointer>::template rebind<const void>`|
|`size_type`|`std::size_t`|
|`difference_type`|`std::ptrdiff_t`|
|`propagate_on_container_move_assignment`|`std::true_type`|

`allocator` 不声明 `propagate_on_container_copy_assignment`、`propagate_on_container_swap` 和 `is_always_equal`。这些成员由 `std::allocator_traits` 提供默认值：

- `propagate_on_container_copy_assignment` 与 `propagate_on_container_swap` 默认为 `std::false_type`；
- `is_always_equal` 根据分配器是否为空类推导。

分配器为空类的条件仍然是：

```C++
Ptr 非空 || memory_resource_traits<Res>::is_always_equal::value
```

## 构造、复制与赋值

| | |
|:-|:-|
|`allocator()`|仅当分配器为空类时参与重载；直接默认构造，不保存指针。|
|`explicit allocator(typename memory_resource_traits<Res>::memory_resource_pointer p)`|仅当分配器非空时参与重载；保存资源指针 `p`。|
|`explicit allocator(Res& resource)`|仅当分配器非空时参与重载；保存指向 `resource` 的资源指针。|
|`allocator(const allocator&)`|复制构造，默认化。空类不复制任何状态；非空类复制资源指针。|
|`allocator& operator=(const allocator&)`|复制赋值，默认化。|
|`template<class U> allocator(const allocator<U, Res, Ptr>&)`|从同资源、同编译期指针的 `U` 分配器转换；非空时复制资源指针。|

非空分配器不能默认构造，这是为了避免生成一个没有资源的分配器。需要非空分配器时，必须传入资源引用或资源指针。

## 成员函数

| | |
|:-|:-|
|`allocate`|分配 `n` 个 `T` 的原始内存 <br>（公开成员函数）|
|`deallocate`|释放由 `allocate` 得到的内存 <br>（公开成员函数）|
|`max_size`|返回一次分配可能请求的最大元素数 <br>（公开静态成员函数）|
|`get_resource_pointer`|返回保存的资源指针；仅非空分配器提供 <br>（公开成员函数）|
|`operator==`|比较两个分配器是否等价 <br>（非成员函数）|

### allocate

```C++
[[nodiscard]] pointer allocate(size_type n);
```

通过资源分配 `n * sizeof(T)` 字节，对齐为 `alignof(T)`，并返回 `pointer`。

若 `n > max_size()`，抛出 `std::bad_array_new_length`。资源自身抛出的异常会原样传播；资源返回空指针时的行为由资源契约决定。

### deallocate

```C++
void deallocate(pointer p, size_type n) noexcept;
```

把 `p` 转换回资源的 `void_pointer`，再以 `n * sizeof(T)` 字节、`alignof(T)` 对齐调用资源的释放接口。

该函数声明为 `noexcept`，因此资源释放不应抛出异常。

### max_size

```C++
[[nodiscard]] static constexpr size_type max_size() noexcept;
```

返回 `std::numeric_limits<size_type>::max() / sizeof(T)`，即大小计算不会溢出的最大元素数。

### get_resource_pointer

```C++
[[nodiscard]] constexpr
    typename memory_resource_traits<Res>::memory_resource_pointer
    get_resource_pointer() const noexcept;
```

返回分配器保存的资源指针。该函数只在非空分配器上提供；空类分配器没有运行时资源指针。

## 比较

```C++
friend bool operator==(const allocator& lhs,
                       const allocator& rhs);
```

- 空类分配器：恒等，返回 `true`。
- 非空分配器：比较保存的资源指针**所指向的资源对象**，即比较 `*lhs.get_resource_pointer()` 与 `*rhs.get_resource_pointer()` 是否相等。

因此，两个分配器即使保存的指针地址不同，只要它们指向的资源对象比较相等，分配器就相等。复制得到的分配器与原分配器相等。

`operator!=` 由 `operator==` 重写生成。

## 缀饰指针（fancy pointer）支持

当资源的 `void_pointer` 是 fancy pointer 时，`pointer` 也是通过 `std::pointer_traits` 重绑定得到的 fancy pointer。分配器在 `allocate` 中把资源返回的 `void_pointer` 转为 `pointer`，在 `deallocate` 中反向转换。

为此，指针族需要满足：

- `std::pointer_traits` 能把 `void_pointer` 重绑定为 `T` 和 `const T`；
- 资源返回的指针能通过 `std::to_address` 取得地址；
- 重绑定后的元素指针与资源 `void_pointer` 之间存在可用的构造或转换。

不满足这些条件的 fancy pointer 会在相应成员函数实例化时编译失败。

## 示例

### 无状态资源与空类分配器

```C++
#include <memory>
#include <type_traits>
#include "mmr.h"

using allocator_type =
    Yc::mmr::allocator<int, Yc::mmr::new_delete_resource>;

static_assert(std::is_empty_v<allocator_type>);
static_assert(std::allocator_traits<allocator_type>::is_always_equal::value);

allocator_type alloc;
int* p = alloc.allocate(1);
std::construct_at(p, 1);
std::destroy_at(p);
alloc.deallocate(p, 1);
```

### 有状态资源

```C++
MyResource resource;
Yc::mmr::allocator<int, MyResource> alloc{resource};

int* p = alloc.allocate(8);
alloc.deallocate(p, 8);
```

此时分配器非空，不能默认构造。

### 编译期资源指针

```C++
inline MyResource resource;

using allocator_type =
    Yc::mmr::allocator<int, MyResource, &resource>;

static_assert(std::is_empty_v<allocator_type>);
allocator_type alloc;
```

对于 fancy 资源指针，编译期实参也必须是同类型的常量，不能直接把普通 `&resource` 传入。

### 与标准容器配合

```C++
#include <vector>

std::vector<int, Yc::mmr::allocator<
    int, Yc::mmr::new_delete_resource>> values;

values.push_back(1);
values.push_back(2);
```

非空分配器也可以作为容器的构造参数传入。

## 注解

- `allocator` 满足标准分配器的基本要求，包括复制和相等比较，可与 `std::allocator_traits` 配合。
- `allocator` 不提供嵌套 `rebind`。需要从 `T` 重绑定到 `U` 时，使用 `std::allocator_traits<allocator<T, Res, Ptr>>::rebind_alloc<U>`；重绑定由 `allocator_traits` 完成，并保留资源类型和编译期资源指针。
- 分配器只管理原始内存，不负责构造或析构 `T` 对象；这些操作由 `std::allocator_traits` 或调用者完成。
- `propagate_on_container_move_assignment` 为 `std::true_type`；容器移动赋值时可以传播该分配器。
- `allocator` 不声明 `propagate_on_container_copy_assignment`、`propagate_on_container_swap` 和 `is_always_equal`；`std::allocator_traits` 为前两者提供 `std::false_type`，并根据空类性质推导 `is_always_equal`。
- 非空分配器的 `get_resource_pointer` 返回保存的指针；空类分配器没有该接口。
- 资源必须在所有使用该分配器的对象之前保持有效。
