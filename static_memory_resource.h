#pragma once
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>

namespace Yc
{
    namespace details
    {
        template<class T, class = void>
        struct memory_resource_void_pointer
        {
            using type = void*;
        };

        template<class T>
        struct memory_resource_void_pointer<T, std::void_t<typename T::void_pointer>>
        {
            using type = typename T::void_pointer;
        };

        template<class T, class = void>
        struct memory_resource_is_always_equal
        {
            using type = std::bool_constant<std::is_empty_v<T>>;
        };

        template<class T>
        struct memory_resource_is_always_equal<T, std::void_t<typename T::is_always_equal>>
        {
            using type = typename T::is_always_equal;
        };

        struct null_memory_resource_pointer
        {
            constexpr null_memory_resource_pointer(std::nullptr_t) noexcept
            {
            }
        };

        template<class P, P>
        struct nttp_value
        {
        };

        template<class P>
        concept structural_pointer =
            std::is_convertible_v<std::nullptr_t, P> &&
            requires { typename nttp_value<P, P{nullptr}>; };

        template<class P>
        using memory_resource_pointer_parameter_t = std::conditional_t<
            structural_pointer<P>,
            P,
            null_memory_resource_pointer>;
    }

    // Use T::void_pointer when it exists; otherwise fall back to void*.
    template<class T>
    struct memory_resource_traits
    {
        using void_pointer = typename details::memory_resource_void_pointer<T>::type;
        using memory_resource_pointer =
            typename std::pointer_traits<void_pointer>::template rebind<T>;
        using is_always_equal = typename details::memory_resource_is_always_equal<T>::type;
    };

    template<class T>
    concept static_memory_resource =
        std::equality_comparable<T> &&
        requires(
            T t,
            typename memory_resource_traits<T>::void_pointer p)
        {
            { t.allocate((std::size_t)0) } -> std::same_as<typename memory_resource_traits<T>::void_pointer>;
            { t.allocate((std::size_t)0, (std::size_t)8) } -> std::same_as<typename memory_resource_traits<T>::void_pointer>;
            t.deallocate(p, (std::size_t)0);
            t.deallocate(p, (std::size_t)0, (std::size_t)8);
        };

    static_assert(static_memory_resource<std::pmr::memory_resource>);

    template<class T>
    struct is_static_memory_resource : std::bool_constant<static_memory_resource<T>>
    {
    };

    template<class T>
    inline constexpr bool is_static_memory_resource_v = is_static_memory_resource<T>::value;

    namespace details
    {
        template<class Res>
        using static_allocator_pointer_t = memory_resource_pointer_parameter_t<
            typename memory_resource_traits<Res>::memory_resource_pointer>;

        template<class Res, auto Ptr>
        struct static_allocator_config
        {
            using resource_type = Res;
            static constexpr auto pointer_value = Ptr;
        };

        template<class P, P Value>
        concept constant_bool_pointer =
            requires { typename std::bool_constant<static_cast<bool>(Value)>; };

        template<class P, P Value>
        concept constant_to_address_pointer =
            requires { typename std::bool_constant<std::to_address(Value) != nullptr>; };

        template<class P, P Value>
        consteval bool is_nonnull_pointer() noexcept
        {
            using pointer_type = std::remove_cv_t<P>;

            if constexpr (std::is_same_v<pointer_type, null_memory_resource_pointer>)
            {
                return false;
            }
            else if constexpr (std::is_null_pointer_v<pointer_type>)
            {
                return false;
            }
            else if constexpr (constant_bool_pointer<P, Value>)
            {
                return static_cast<bool>(Value);
            }
            else if constexpr (constant_to_address_pointer<P, Value>)
            {
                return std::to_address(Value) != nullptr;
            }
            else
            {
                return false;
            }
        }

        template<class Res, class Pointer, auto Ptr, bool Empty>
        class allocator_storage;

        // No state is stored when a compile-time pointer is available or the resource is stateless.
        template<class Res, class Pointer, auto Ptr>
        class allocator_storage<Res, Pointer, Ptr, true>
        {
        public:
            constexpr allocator_storage() noexcept = default;

            static Res& resource() noexcept
            {
                if constexpr (is_nonnull_pointer<decltype(Ptr), Ptr>())
                {
                    return *std::to_address(Ptr);
                }
                else
                {
                    return default_resource();
                }
            }

        private:
            static Res& default_resource()
            {
                static Res resource{};
                return resource;
            }
        };

        // A stateful resource without a compile-time pointer stores a resource pointer.
        template<class Res, class Pointer, auto Ptr>
        class allocator_storage<Res, Pointer, Ptr, false>
        {
            Pointer resource_;

        public:
            constexpr explicit allocator_storage(Pointer p) noexcept
                : resource_(p)
            {
            }

            Res& resource() const noexcept
            {
                return *std::to_address(resource_);
            }

            constexpr Pointer get_pointer() const noexcept
            {
                return resource_;
            }
        };

        template<class Config>
        using static_allocator_storage_t = allocator_storage<
            typename Config::resource_type,
            typename memory_resource_traits<
                typename Config::resource_type>::memory_resource_pointer,
            Config::pointer_value,
            is_nonnull_pointer<decltype(Config::pointer_value), Config::pointer_value>() ||
                memory_resource_traits<
                    typename Config::resource_type>::is_always_equal::value>;
    }

    namespace details
    {
        template<class T, class Config>
            requires static_memory_resource<typename Config::resource_type>
        class static_allocator_impl
            : private static_allocator_storage_t<Config>
        {
            using resource_type = typename Config::resource_type;
            static constexpr auto Ptr = Config::pointer_value;
            using resource_pointer =
                typename memory_resource_traits<resource_type>::memory_resource_pointer;
            using resource_void_pointer =
                typename memory_resource_traits<resource_type>::void_pointer;
            static constexpr bool is_empty_resource =
                is_nonnull_pointer<decltype(Ptr), Ptr>() ||
                memory_resource_traits<resource_type>::is_always_equal::value;
            using storage_type = static_allocator_storage_t<Config>;

        public:
            using value_type = T;
            using pointer = typename std::pointer_traits<resource_void_pointer>::template rebind<T>;
            using const_pointer = typename std::pointer_traits<resource_void_pointer>::template rebind<const T>;
            using void_pointer = resource_void_pointer;
            using const_void_pointer =
                typename std::pointer_traits<resource_void_pointer>::template rebind<const void>;
            using size_type = std::size_t;
            using difference_type = std::ptrdiff_t;
            using propagate_on_container_move_assignment = std::true_type;

            constexpr static_allocator_impl() noexcept
                requires is_empty_resource
                = default;

            static_allocator_impl(const static_allocator_impl&) = default;
            static_allocator_impl& operator=(const static_allocator_impl&) = default;

            constexpr explicit static_allocator_impl(resource_pointer p) noexcept
                requires (!is_empty_resource)
                : storage_type(p)
            {
            }

            constexpr explicit static_allocator_impl(resource_type& resource) noexcept
                requires (!is_empty_resource)
                : storage_type(std::pointer_traits<resource_pointer>::pointer_to(resource))
            {
            }

            template<class U>
            constexpr static_allocator_impl(const static_allocator_impl<U, Config>&) noexcept
                requires is_empty_resource
                : storage_type()
            {
            }

            template<class U>
            constexpr static_allocator_impl(
                const static_allocator_impl<U, Config>& other) noexcept
                requires (!is_empty_resource)
                : storage_type(other.get_resource_pointer())
            {
            }

            [[nodiscard]] pointer allocate(size_type n)
            {
                if (n > max_size())
                {
                    throw std::bad_array_new_length();
                }

                const auto p = this->resource().allocate(n * sizeof(T), alignof(T));
                return to_allocator_pointer(p);
            }

            void deallocate(pointer p, size_type n) noexcept
            {
                this->resource().deallocate(
                    to_resource_void_pointer(p),
                    n * sizeof(T),
                    alignof(T));
            }

            [[nodiscard]] static constexpr size_type max_size() noexcept
            {
                return (std::numeric_limits<size_type>::max)() / sizeof(T);
            }

            [[nodiscard]] constexpr resource_pointer get_resource_pointer() const noexcept
                requires (!is_empty_resource)
            {
                return static_cast<const storage_type&>(*this).get_pointer();
            }

            friend bool operator==(
                const static_allocator_impl& lhs,
                const static_allocator_impl& rhs)
            {
                if constexpr (is_empty_resource)
                {
                    return true;
                }
                else
                {
                    return lhs.resource() == rhs.resource();
                }
            }

        private:
            static pointer to_allocator_pointer(resource_void_pointer p) noexcept
            {
                using raw_pointer = decltype(std::to_address(p));
                if constexpr (std::is_constructible_v<pointer, resource_void_pointer>)
                {
                    return static_cast<pointer>(p);
                }
                else if constexpr (std::is_constructible_v<pointer, raw_pointer>)
                {
                    return static_cast<pointer>(std::to_address(p));
                }
                else
                {
                    return static_cast<pointer>(
                        static_cast<T*>(static_cast<void*>(std::to_address(p))));
                }
            }

            static resource_void_pointer to_resource_void_pointer(pointer p) noexcept
            {
                using raw_pointer = decltype(std::to_address(p));
                if constexpr (std::is_constructible_v<resource_void_pointer, pointer>)
                {
                    return static_cast<resource_void_pointer>(p);
                }
                else if constexpr (std::is_constructible_v<resource_void_pointer, raw_pointer>)
                {
                    return static_cast<resource_void_pointer>(std::to_address(p));
                }
                else
                {
                    return static_cast<resource_void_pointer>(
                        static_cast<void*>(std::to_address(p)));
                }
            }
        };
    }

    template<class T, static_memory_resource Res,
        details::static_allocator_pointer_t<Res> Ptr = nullptr>
    using static_allocator = details::static_allocator_impl<
        T, details::static_allocator_config<Res, Ptr>>;

    struct static_new_delete_resource
    {
        using void_pointer = void*;

        static constexpr std::size_t default_alignment =
            __STDCPP_DEFAULT_NEW_ALIGNMENT__;

        [[nodiscard]] void* allocate(
            std::size_t bytes,
            std::size_t alignment = default_alignment)
        {
            if (alignment > default_alignment)
            {
                return ::operator new(bytes, std::align_val_t{alignment});
            }
            return ::operator new(bytes);
        }

        void deallocate(
            void* p,
            std::size_t,
            std::size_t alignment = default_alignment) noexcept
        {
            if (alignment > default_alignment)
            {
                ::operator delete(p, std::align_val_t{alignment});
            }
            else
            {
                ::operator delete(p);
            }
        }

        bool operator==(const static_new_delete_resource&) const noexcept = default;
    };

    struct static_null_memory_resource
    {
        using void_pointer = void*;

        void* allocate(
            std::size_t,
            std::size_t = alignof(std::max_align_t))
        {
            throw std::bad_alloc{};
        }

        void deallocate(
            void*,
            std::size_t,
            std::size_t = alignof(std::max_align_t)) noexcept
        {
        }

        bool operator==(const static_null_memory_resource&) const noexcept = default;
    };

    static_assert(static_memory_resource<static_new_delete_resource>);
    static_assert(static_memory_resource<static_null_memory_resource>);
    static_assert(std::is_empty_v<static_new_delete_resource>);
    static_assert(std::is_empty_v<static_null_memory_resource>);

    template<auto Buffer = nullptr, std::size_t Length = 0,
        static_memory_resource Upstream = static_new_delete_resource,
        details::static_allocator_pointer_t<Upstream> UpstreamPtr = nullptr>
    class static_monotonic_buffer_resource
        : private details::allocator_storage<
            Upstream, typename memory_resource_traits<Upstream>::memory_resource_pointer,
            UpstreamPtr,
            details::is_nonnull_pointer<decltype(UpstreamPtr), UpstreamPtr>() ||
                memory_resource_traits<Upstream>::is_always_equal::value>
    {
        using upstream_pointer =
            typename memory_resource_traits<Upstream>::memory_resource_pointer;

        using resource_void_pointer =
            typename memory_resource_traits<Upstream>::void_pointer;

        static constexpr bool has_static_buffer =
            details::is_nonnull_pointer<decltype(Buffer), Buffer>();
        static constexpr bool static_upstream =
            details::is_nonnull_pointer<decltype(UpstreamPtr), UpstreamPtr>() ||
            memory_resource_traits<Upstream>::is_always_equal::value;
        static constexpr std::size_t default_alignment = alignof(std::max_align_t);

        using upstream_storage = details::allocator_storage<
            Upstream, upstream_pointer, UpstreamPtr, static_upstream>;

        struct block;

        using block_pointer = typename std::pointer_traits<
            resource_void_pointer>::template rebind<block>;
        using byte_pointer = typename std::pointer_traits<
            resource_void_pointer>::template rebind<std::byte>;

        struct block
        {
            resource_void_pointer pointer;
            std::size_t bytes;
            std::size_t alignment;
            block_pointer next;
        };

    public:
        using void_pointer = resource_void_pointer;
        using upstream_type = Upstream;
        using upstream_resource_pointer = upstream_pointer;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        static_assert(
            !has_static_buffer || std::is_same_v<decltype(Buffer), void_pointer>,
            "static buffer address type must match Upstream::void_pointer");
        static_assert(
            has_static_buffer || Length == 0,
            "a null static buffer address requires Length == 0");

        static_monotonic_buffer_resource()
            requires (has_static_buffer && static_upstream)
        {
            set_static_buffer();
        }

        explicit static_monotonic_buffer_resource(upstream_pointer upstream_resource)
            requires (has_static_buffer && !static_upstream)
            : upstream_storage(upstream_resource)
        {
            set_static_buffer();
        }

        explicit static_monotonic_buffer_resource(Upstream& upstream_resource)
            requires (has_static_buffer && !static_upstream)
            : upstream_storage(make_upstream_pointer(upstream_resource))
        {
            set_static_buffer();
        }

        static_monotonic_buffer_resource(void_pointer initial_buffer, size_type length)
            requires (!has_static_buffer && static_upstream)
        {
            set_buffer(initial_buffer, length);
        }

        static_monotonic_buffer_resource(
            void_pointer initial_buffer,
            size_type length,
            upstream_pointer upstream_resource)
            requires (!has_static_buffer && !static_upstream)
            : upstream_storage(upstream_resource)
        {
            set_buffer(initial_buffer, length);
        }

        static_monotonic_buffer_resource(
            void_pointer initial_buffer,
            size_type length,
            Upstream& upstream_resource)
            requires (!has_static_buffer && !static_upstream)
            : upstream_storage(make_upstream_pointer(upstream_resource))
        {
            set_buffer(initial_buffer, length);
        }

        ~static_monotonic_buffer_resource()
        {
            release_blocks();
        }

        static_monotonic_buffer_resource(const static_monotonic_buffer_resource&) = delete;
        static_monotonic_buffer_resource& operator=(const static_monotonic_buffer_resource&) = delete;
        static_monotonic_buffer_resource(static_monotonic_buffer_resource&&) = delete;
        static_monotonic_buffer_resource& operator=(static_monotonic_buffer_resource&&) = delete;

        [[nodiscard]] void_pointer allocate(
            size_type bytes,
            size_type alignment = default_alignment)
        {
            alignment = alignment == 0 ? 1 : alignment;

            if (std::to_address(current_) != nullptr)
            {
                auto* raw_current = std::to_address(current_);
                auto* raw_end = std::to_address(end_);
                void* data = raw_current;
                size_type space = static_cast<size_type>(raw_end - raw_current);
                if (std::align(alignment, bytes, data, space) != nullptr)
                {
                    auto* aligned = static_cast<std::byte*>(data);
                    current_ = make_byte_pointer(aligned + bytes);
                    return make_void_pointer(aligned);
                }
            }

            return make_void_pointer(allocate_block(bytes, alignment));
        }

        void deallocate(
            void_pointer,
            size_type,
            size_type = default_alignment) noexcept
        {
        }

        void release() noexcept
        {
            release_blocks();
            current_ = initial_begin_;
            end_ = initial_end_;
        }

        [[nodiscard]] upstream_resource_pointer upstream_resource() const noexcept
        {
            if constexpr (static_upstream)
            {
                if constexpr (details::is_nonnull_pointer<decltype(UpstreamPtr), UpstreamPtr>())
                {
                    return static_cast<upstream_resource_pointer>(UpstreamPtr);
                }
                else
                {
                    return make_upstream_pointer(this->resource());
                }
            }
            else
            {
                return static_cast<const upstream_storage&>(*this).get_pointer();
            }
        }

        bool operator==(const static_monotonic_buffer_resource& other) const noexcept
        {
            return this == &other;
        }

    private:
        byte_pointer current_{};
        byte_pointer end_{};
        byte_pointer initial_begin_{};
        byte_pointer initial_end_{};
        block_pointer blocks_{};

        void set_static_buffer() noexcept
        {
            current_ = make_byte_pointer(static_buffer_begin());
            end_ = make_byte_pointer(static_buffer_end());
            initial_begin_ = current_;
            initial_end_ = end_;
        }

        static std::byte* static_buffer_begin() noexcept
        {
            if constexpr (has_static_buffer)
            {
                return static_cast<std::byte*>(
                    static_cast<void*>(std::to_address(Buffer)));
            }
            else
            {
                return nullptr;
            }
        }

        static std::byte* static_buffer_end() noexcept
        {
            if constexpr (has_static_buffer)
            {
                return static_buffer_begin() + Length;
            }
            else
            {
                return nullptr;
            }
        }

        static byte_pointer make_byte_pointer(std::byte* p) noexcept
        {
            if constexpr (std::is_constructible_v<byte_pointer, std::byte*>)
            {
                return static_cast<byte_pointer>(p);
            }
            else
            {
                return static_cast<byte_pointer>(static_cast<void*>(p));
            }
        }

        static upstream_pointer make_upstream_pointer(Upstream& upstream_resource) noexcept
        {
            if constexpr (std::is_constructible_v<upstream_pointer, Upstream*>)
            {
                return static_cast<upstream_pointer>(&upstream_resource);
            }
            else
            {
                return std::pointer_traits<upstream_pointer>::pointer_to(upstream_resource);
            }
        }

        static block_pointer make_block_pointer(resource_void_pointer p) noexcept
        {
            if constexpr (std::is_constructible_v<block_pointer, resource_void_pointer>)
            {
                return static_cast<block_pointer>(p);
            }
            else if constexpr (std::is_constructible_v<block_pointer, block*>)
            {
                auto* raw = static_cast<block*>(
                    static_cast<void*>(std::to_address(p)));
                return static_cast<block_pointer>(raw);
            }
            else
            {
                return static_cast<block_pointer>(
                    static_cast<void*>(std::to_address(p)));
            }
        }

        static void_pointer make_void_pointer(std::byte* p) noexcept
        {
            if constexpr (std::is_constructible_v<void_pointer, std::byte*>)
            {
                return static_cast<void_pointer>(p);
            }
            else
            {
                return static_cast<void_pointer>(static_cast<void*>(p));
            }
        }

        void set_buffer(void_pointer initial_buffer, size_type length) noexcept
        {
            void* raw = std::to_address(initial_buffer);
            if (raw == nullptr || length == 0)
            {
                current_ = byte_pointer{};
                end_ = byte_pointer{};
                initial_begin_ = byte_pointer{};
                initial_end_ = byte_pointer{};
                return;
            }

            auto* bytes = static_cast<std::byte*>(raw);
            initial_begin_ = make_byte_pointer(bytes);
            initial_end_ = make_byte_pointer(bytes + length);
            current_ = initial_begin_;
            end_ = initial_end_;
        }

        std::byte* allocate_block(size_type bytes, size_type alignment)
        {
            const size_type data_alignment = alignment == 0 ? 1 : alignment;
            const size_type block_alignment =
                data_alignment > alignof(block) ? data_alignment : alignof(block);

            const size_type max = (std::numeric_limits<size_type>::max)();
            if (data_alignment - 1 > max - sizeof(block))
            {
                throw std::bad_alloc{};
            }

            const size_type overhead = sizeof(block) + data_alignment - 1;
            if (bytes > max - overhead)
            {
                throw std::bad_alloc{};
            }

            const size_type total = bytes + overhead;
            auto p = this->resource().allocate(total, block_alignment);
            auto* raw = static_cast<std::byte*>(
                static_cast<void*>(std::to_address(p)));

            void* data = raw + sizeof(block);
            size_type space = total - sizeof(block);
            data = std::align(data_alignment, bytes, data, space);
            if (data == nullptr)
            {
                this->resource().deallocate(p, total, block_alignment);
                throw std::bad_alloc{};
            }

            ::new (static_cast<void*>(raw))
                block{p, total, block_alignment, blocks_};
            blocks_ = make_block_pointer(p);

            auto* aligned_data = static_cast<std::byte*>(data);
            current_ = make_byte_pointer(aligned_data + bytes);
            end_ = make_byte_pointer(aligned_data + space);
            return aligned_data;
        }

        void release_blocks() noexcept
        {
            block_pointer current = blocks_;
            while (std::to_address(current) != nullptr)
            {
                block* raw = std::to_address(current);
                block_pointer next = raw->next;
                const auto p = raw->pointer;
                const auto bytes = raw->bytes;
                const auto alignment = raw->alignment;

                raw->~block();
                this->resource().deallocate(p, bytes, alignment);
                current = next;
            }

            blocks_ = block_pointer{};
        }
    };

    static_assert(static_memory_resource<static_monotonic_buffer_resource<>>);
}