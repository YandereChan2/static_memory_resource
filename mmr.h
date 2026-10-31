#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>

namespace Yc::mmr
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
    concept memory_resource =
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

    static_assert(memory_resource<std::pmr::memory_resource>);

    template<class T>
    struct is_memory_resource : std::bool_constant<memory_resource<T>>
    {
    };

    template<class T>
    inline constexpr bool is_memory_resource_v = is_memory_resource<T>::value;

    namespace details
    {
        template<class Res>
        using allocator_pointer_t = memory_resource_pointer_parameter_t<
            typename memory_resource_traits<Res>::memory_resource_pointer>;

        template<class Res, auto Ptr>
        struct allocator_config
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
        using allocator_storage_t = allocator_storage<
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
            requires memory_resource<typename Config::resource_type>
        class allocator_impl
            : private allocator_storage_t<Config>
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
            using storage_type = allocator_storage_t<Config>;

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

            constexpr allocator_impl() noexcept
                requires is_empty_resource
                = default;

            allocator_impl(const allocator_impl&) = default;
            allocator_impl& operator=(const allocator_impl&) = default;

            constexpr explicit allocator_impl(resource_pointer p) noexcept
                requires (!is_empty_resource)
                : storage_type(p)
            {
            }

            template<class U>
            constexpr allocator_impl(const allocator_impl<U, Config>&) noexcept
                requires is_empty_resource
                : storage_type()
            {
            }

            template<class U>
            constexpr allocator_impl(
                const allocator_impl<U, Config>& other) noexcept
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
                const allocator_impl& lhs,
                const allocator_impl& rhs)
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

    template<class T, memory_resource Res,
        details::allocator_pointer_t<Res> Ptr = nullptr>
    using allocator = details::allocator_impl<
        T, details::allocator_config<Res, Ptr>>;

    struct new_delete_resource
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

        bool operator==(const new_delete_resource&) const noexcept = default;
    };

    struct null_memory_resource
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

        bool operator==(const null_memory_resource&) const noexcept = default;
    };

    static_assert(memory_resource<new_delete_resource>);
    static_assert(memory_resource<null_memory_resource>);
    static_assert(std::is_empty_v<new_delete_resource>);
    static_assert(std::is_empty_v<null_memory_resource>);

    template<auto Buffer = nullptr, std::size_t Length = 0,
        memory_resource Upstream = new_delete_resource,
        details::allocator_pointer_t<Upstream> UpstreamPtr = nullptr>
    class monotonic_buffer_resource
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
        static constexpr bool compile_time_upstream =
            details::is_nonnull_pointer<decltype(UpstreamPtr), UpstreamPtr>() ||
            memory_resource_traits<Upstream>::is_always_equal::value;
        static constexpr std::size_t default_alignment = alignof(std::max_align_t);

        using upstream_storage = details::allocator_storage<
            Upstream, upstream_pointer, UpstreamPtr, compile_time_upstream>;

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

        monotonic_buffer_resource()
            requires (has_static_buffer && compile_time_upstream)
        {
            set_static_buffer();
        }

        explicit monotonic_buffer_resource(upstream_pointer upstream_resource)
            requires (has_static_buffer && !compile_time_upstream)
            : upstream_storage(upstream_resource)
        {
            set_static_buffer();
        }

        explicit monotonic_buffer_resource(Upstream& upstream_resource)
            requires (has_static_buffer && !compile_time_upstream)
            : upstream_storage(make_upstream_pointer(upstream_resource))
        {
            set_static_buffer();
        }

        monotonic_buffer_resource(void_pointer initial_buffer, size_type length)
            requires (!has_static_buffer && compile_time_upstream)
        {
            set_buffer(initial_buffer, length);
        }

        monotonic_buffer_resource(
            void_pointer initial_buffer,
            size_type length,
            upstream_pointer upstream_resource)
            requires (!has_static_buffer && !compile_time_upstream)
            : upstream_storage(upstream_resource)
        {
            set_buffer(initial_buffer, length);
        }

        monotonic_buffer_resource(
            void_pointer initial_buffer,
            size_type length,
            Upstream& upstream_resource)
            requires (!has_static_buffer && !compile_time_upstream)
            : upstream_storage(make_upstream_pointer(upstream_resource))
        {
            set_buffer(initial_buffer, length);
        }

        ~monotonic_buffer_resource()
        {
            release_blocks();
        }

        monotonic_buffer_resource(const monotonic_buffer_resource&) = delete;
        monotonic_buffer_resource& operator=(const monotonic_buffer_resource&) = delete;
        monotonic_buffer_resource(monotonic_buffer_resource&&) = delete;
        monotonic_buffer_resource& operator=(monotonic_buffer_resource&&) = delete;

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
            if constexpr (compile_time_upstream)
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

        bool operator==(const monotonic_buffer_resource& other) const noexcept
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

    template<
        std::size_t ChunkSize = 4096,
        memory_resource Upstream = new_delete_resource,
        details::allocator_pointer_t<Upstream> UpstreamPtr = nullptr>
    class lifo_pool_resource
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
        static constexpr bool compile_time_upstream =
            details::is_nonnull_pointer<decltype(UpstreamPtr), UpstreamPtr>() ||
            memory_resource_traits<Upstream>::is_always_equal::value;
        using upstream_storage = details::allocator_storage<
            Upstream, upstream_pointer, UpstreamPtr, compile_time_upstream>;

        struct chunk_header;
        using chunk_pointer =
            typename std::pointer_traits<resource_void_pointer>::template rebind<chunk_header>;

        struct chunk_header
        {
            chunk_pointer prev;
            resource_void_pointer base;
            std::uint32_t high_water;
            std::uint32_t reserved;
        };

        struct direct_header_fields;
        using direct_header_pointer = typename std::pointer_traits<
            resource_void_pointer>::template rebind<direct_header_fields>;

        struct direct_header_fields
        {
            resource_void_pointer base;
            direct_header_pointer prev;
            direct_header_pointer next;
            std::size_t total;
            std::size_t alignment;
        };

        static constexpr std::size_t align_up_constant(
            std::size_t value,
            std::size_t alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        static constexpr std::size_t chunk_header_size =
            align_up_constant(sizeof(chunk_header), 16);
        static constexpr std::size_t first_block_offset = chunk_header_size + 8;
        static constexpr std::size_t epilogue_offset = ChunkSize - 8;
        static constexpr std::size_t chunk_capacity = epilogue_offset - first_block_offset;
        static constexpr std::size_t direct_header_size = align_up_constant(
            sizeof(direct_header_fields) + 8, alignof(direct_header_fields));

        static_assert(ChunkSize >= 256);
        static_assert((ChunkSize & (ChunkSize - 1)) == 0);
        static_assert(ChunkSize <= (std::numeric_limits<std::uint32_t>::max)());
        static_assert(chunk_capacity >= 16);
        static_assert(direct_header_size >= sizeof(direct_header_fields) + 8);

        static constexpr std::uint64_t used_bit = 1;
        static constexpr std::uint64_t previous_used_bit = 2;
        static constexpr std::uint64_t direct_block_bit = 4;
        static constexpr std::uint64_t flag_mask = 0xF;
        static constexpr std::size_t block_overhead = 16;
        static constexpr std::size_t minimum_gap_block = 16;

    public:
        using void_pointer = resource_void_pointer;
        using upstream_type = Upstream;
        using upstream_resource_pointer = upstream_pointer;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

        static constexpr size_type chunk_size = ChunkSize;
        static constexpr size_type chunk_alignment = ChunkSize;
        static constexpr size_type default_alignment = alignof(std::max_align_t);

        constexpr lifo_pool_resource() noexcept
            requires compile_time_upstream
        = default;

        explicit lifo_pool_resource(upstream_pointer upstream_resource)
            requires (!compile_time_upstream)
            : upstream_storage(upstream_resource)
        {
        }

        explicit lifo_pool_resource(Upstream& upstream_resource)
            requires (!compile_time_upstream)
            : upstream_storage(make_upstream_pointer(upstream_resource))
        {
        }

        ~lifo_pool_resource()
        {
            release_direct_blocks();
            release_chunks();
        }

        lifo_pool_resource(const lifo_pool_resource&) = delete;
        lifo_pool_resource& operator=(const lifo_pool_resource&) = delete;
        lifo_pool_resource(lifo_pool_resource&&) = delete;
        lifo_pool_resource& operator=(lifo_pool_resource&&) = delete;

        [[nodiscard]] void_pointer allocate(
            size_type bytes,
            size_type alignment = default_alignment)
        {
            alignment = alignment == 0 ? 1 : alignment;

            if (bytes > (std::numeric_limits<size_type>::max)() - 15)
            {
                throw std::bad_alloc{};
            }

            const size_type payload = (bytes + 15) & ~size_type{15};
            if (payload > (std::numeric_limits<size_type>::max)() - block_overhead)
            {
                throw std::bad_alloc{};
            }

            const size_type total = payload + block_overhead;

            if (!fits_in_fresh_chunk(total, alignment))
            {
                return direct_allocate(bytes, alignment);
            }

            if (std::to_address(current_chunk_) == nullptr)
            {
                create_chunk();
            }

            void* result = nullptr;
            if (try_bump_allocate(total, alignment, result))
            {
                return make_void_pointer(static_cast<std::byte*>(result));
            }

            create_chunk();
            if (try_bump_allocate(total, alignment, result))
            {
                return make_void_pointer(static_cast<std::byte*>(result));
            }

            release_empty_current_chunks();
            return direct_allocate(bytes, alignment);
        }

        void deallocate(
            void_pointer p,
            size_type,
            size_type = default_alignment) noexcept
        {
            auto* raw = static_cast<std::byte*>(
                static_cast<void*>(std::to_address(p)));
            if (raw == nullptr)
            {
                return;
            }

            const std::uint64_t tag = load_tag(raw - 8);
            if ((tag & direct_block_bit) != 0)
            {
                direct_deallocate(raw);
            }
            else
            {
                chunk_deallocate(raw);
            }
        }

        [[nodiscard]] upstream_resource_pointer upstream_resource() const noexcept
        {
            if constexpr (compile_time_upstream)
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

        bool operator==(const lifo_pool_resource& other) const noexcept
        {
            return this == &other;
        }

    private:
        chunk_pointer current_chunk_{};
        direct_header_pointer direct_blocks_{};

        static std::uint64_t load_tag(const std::byte* p) noexcept
        {
            std::uint64_t value{};
            std::memcpy(&value, p, sizeof(value));
            return value;
        }

        static void store_tag(std::byte* p, std::uint64_t value) noexcept
        {
            std::memcpy(p, &value, sizeof(value));
        }

        static bool checked_align_up(
            size_type value,
            size_type alignment,
            size_type& result) noexcept
        {
            if (alignment == 0)
            {
                alignment = 1;
            }

            if (value > (std::numeric_limits<size_type>::max)() - (alignment - 1))
            {
                return false;
            }

            result = (value + alignment - 1) & ~(alignment - 1);
            return true;
        }

        static std::byte* chunk_base_of(std::byte* p) noexcept
        {
            const auto address = reinterpret_cast<std::uintptr_t>(p);
            return reinterpret_cast<std::byte*>(
                address & ~(static_cast<std::uintptr_t>(chunk_size) - 1));
        }

        bool fits_in_fresh_chunk(size_type total, size_type alignment) const noexcept
        {
            size_type payload_offset = 0;
            if (!checked_align_up(first_block_offset + 8, alignment, payload_offset))
            {
                return false;
            }

            const size_type block_offset = payload_offset - 8;
            const size_type gap = block_offset - first_block_offset;
            return gap <= chunk_capacity && total <= chunk_capacity - gap;
        }

        chunk_header* current_header() const noexcept
        {
            auto* raw = std::to_address(current_chunk_);
            return static_cast<chunk_header*>(static_cast<void*>(raw));
        }

        void create_chunk()
        {
            void_pointer base = this->resource().allocate(chunk_size, chunk_alignment);
            auto* raw = static_cast<std::byte*>(
                static_cast<void*>(std::to_address(base)));
            if (raw == nullptr)
            {
                throw std::bad_alloc{};
            }

            auto* header = reinterpret_cast<chunk_header*>(raw);
            ::new (static_cast<void*>(header)) chunk_header{};
            header->prev = current_chunk_;
            header->base = base;
            header->high_water = static_cast<std::uint32_t>(first_block_offset);
            header->reserved = 0;

            store_tag(raw + chunk_header_size, used_bit);
            store_tag(raw + epilogue_offset, used_bit);

            current_chunk_ = make_chunk_pointer(*header);
        }

        bool try_bump_allocate(
            size_type total,
            size_type alignment,
            void*& result) noexcept
        {
            chunk_header* header = current_header();
            if (header == nullptr)
            {
                return false;
            }

            const size_type old_high_water = header->high_water;
            size_type payload_offset = 0;
            if (!checked_align_up(old_high_water + 8, alignment, payload_offset))
            {
                return false;
            }

            const size_type block_offset = payload_offset - 8;
            if (block_offset < old_high_water || block_offset + total > epilogue_offset)
            {
                return false;
            }

            auto* base = static_cast<std::byte*>(
                static_cast<void*>(std::to_address(header->base)));
            const size_type gap = block_offset - old_high_water;
            bool previous_used = true;

            if (gap >= minimum_gap_block)
            {
                store_tag(base + old_high_water, gap | previous_used_bit);
                store_tag(base + block_offset - 8, gap);
                previous_used = false;
            }
            else if (gap != 0)
            {
                previous_used = absorb_gap(base, old_high_water, block_offset, gap);
            }

            const std::uint64_t flags =
                used_bit | (previous_used ? previous_used_bit : 0);
            store_tag(base + block_offset, total | flags);
            store_tag(base + block_offset + total - 8, total);
            header->high_water = static_cast<std::uint32_t>(block_offset + total);

            if (header->high_water == epilogue_offset)
            {
                store_tag(base + epilogue_offset, used_bit | previous_used_bit);
            }

            result = base + payload_offset;
            return true;
        }

        bool absorb_gap(
            std::byte* base,
            size_type old_high_water,
            size_type block_offset,
            size_type gap) noexcept
        {
            if (old_high_water == first_block_offset)
            {
                return true;
            }

            const size_type previous_total =
                load_tag(base + old_high_water - 8) & ~flag_mask;
            if (previous_total == 0 ||
                previous_total > old_high_water - first_block_offset)
            {
                return true;
            }

            auto* previous_header = base + old_high_water - previous_total;
            const std::uint64_t previous_tag = load_tag(previous_header);
            const size_type merged_total = previous_total + gap;
            store_tag(
                previous_header,
                merged_total | (previous_tag & flag_mask));
            store_tag(base + block_offset - 8, merged_total);

            return (previous_tag & used_bit) != 0;
        }

        void chunk_deallocate(std::byte* p) noexcept
        {
            auto* header = p - 8;
            std::uint64_t tag = load_tag(header);
            size_type total = tag & ~flag_mask;

            auto* chunk_base = chunk_base_of(header);
            auto* chunk = reinterpret_cast<chunk_header*>(chunk_base);
            const std::uint64_t previous_flag = tag & previous_used_bit;
            const bool previous_used = previous_flag != 0;

            store_tag(header, total | (tag & flag_mask & ~used_bit));

            size_type high_water = chunk->high_water;
            const size_type block_offset =
                static_cast<size_type>(header - chunk_base);
            const bool is_top = block_offset + total == high_water;

            if (is_top)
            {
                bool merged_previous_used = previous_used;
                if (!previous_used)
                {
                    const size_type previous_total =
                        load_tag(header - 8) & ~flag_mask;
                    if (previous_total != 0 &&
                        previous_total <= block_offset - first_block_offset)
                    {
                        auto* previous_header = header - previous_total;
                        const std::uint64_t previous_tag = load_tag(previous_header);
                        total += previous_total;
                        header = previous_header;
                        merged_previous_used =
                            (previous_tag & previous_used_bit) != 0;
                    }
                }

                const size_type new_offset =
                    static_cast<size_type>(header - chunk_base);
                store_tag(
                    header,
                    total | (merged_previous_used ? previous_used_bit : 0));
                store_tag(header + total - 8, total);
                chunk->high_water = static_cast<std::uint32_t>(new_offset);
            }
            else
            {
                auto* next = header + total;
                std::uint64_t next_tag = load_tag(next);
                store_tag(next, next_tag & ~previous_used_bit);

                if ((next_tag & used_bit) == 0)
                {
                    total += next_tag & ~flag_mask;
                }

                bool merged_previous_used = previous_used;
                if (!previous_used)
                {
                    const size_type previous_total =
                        load_tag(header - 8) & ~flag_mask;
                    if (previous_total != 0 &&
                        previous_total <= block_offset - first_block_offset)
                    {
                        auto* previous_header = header - previous_total;
                        const std::uint64_t previous_tag = load_tag(previous_header);
                        header = previous_header;
                        total += previous_total;
                        merged_previous_used =
                            (previous_tag & previous_used_bit) != 0;
                    }
                }

                store_tag(
                    header,
                    total | (merged_previous_used ? previous_used_bit : 0));
                store_tag(header + total - 8, total);

                auto* after = header + total;
                if (after == chunk_base + high_water)
                {
                    chunk->high_water = static_cast<std::uint32_t>(
                        static_cast<size_type>(header - chunk_base));
                }
                else
                {
                    std::uint64_t after_tag = load_tag(after);
                    store_tag(after, after_tag & ~previous_used_bit);
                }
            }

            if (std::to_address(current_chunk_) == nullptr)
            {
                return;
            }

            if (static_cast<void*>(std::to_address(current_chunk_)) ==
                static_cast<void*>(chunk) &&
                chunk->high_water == first_block_offset)
            {
                release_empty_current_chunks();
            }
        }

        void release_empty_current_chunks() noexcept
        {
            while (true)
            {
                auto* chunk = std::to_address(current_chunk_);
                if (chunk == nullptr || chunk->high_water != first_block_offset)
                {
                    return;
                }

                chunk_pointer previous = chunk->prev;
                void_pointer base = chunk->base;
                chunk->~chunk_header();
                this->resource().deallocate(base, chunk_size, chunk_alignment);
                current_chunk_ = previous;
            }
        }

        void release_chunks() noexcept
        {
            while (std::to_address(current_chunk_) != nullptr)
            {
                auto* chunk = std::to_address(current_chunk_);
                chunk_pointer previous = chunk->prev;
                void_pointer base = chunk->base;
                chunk->~chunk_header();
                this->resource().deallocate(base, chunk_size, chunk_alignment);
                current_chunk_ = previous;
            }
        }

        void release_direct_blocks() noexcept
        {
            while (std::to_address(direct_blocks_) != nullptr)
            {
                auto* fields = std::to_address(direct_blocks_);
                direct_header_pointer next = fields->next;
                void_pointer base = fields->base;
                const size_type total = fields->total;
                const size_type alignment = fields->alignment;

                fields->~direct_header_fields();
                this->resource().deallocate(base, total, alignment);
                direct_blocks_ = next;
            }
        }

        void direct_deallocate(std::byte* p) noexcept
        {
            auto* fields = reinterpret_cast<direct_header_fields*>(
                p - direct_header_size);
            direct_header_pointer previous = fields->prev;
            direct_header_pointer next = fields->next;

            if (std::to_address(previous) == nullptr)
            {
                direct_blocks_ = next;
            }
            else
            {
                std::to_address(previous)->next = next;
            }

            if (std::to_address(next) != nullptr)
            {
                std::to_address(next)->prev = previous;
            }

            void_pointer base = fields->base;
            const size_type total = fields->total;
            const size_type alignment = fields->alignment;
            fields->~direct_header_fields();
            this->resource().deallocate(base, total, alignment);
        }

        void_pointer direct_allocate(size_type bytes, size_type alignment)
        {
            const size_type effective_alignment =
                alignment > alignof(direct_header_fields)
                    ? alignment
                    : alignof(direct_header_fields);
            const size_type max = (std::numeric_limits<size_type>::max)();

            if (effective_alignment - 1 > max - direct_header_size)
            {
                throw std::bad_alloc{};
            }

            const size_type overhead = direct_header_size + effective_alignment - 1;
            if (bytes > max - overhead)
            {
                throw std::bad_alloc{};
            }

            const size_type total = bytes + overhead;
            void_pointer base = this->resource().allocate(total, effective_alignment);
            auto* raw_base = static_cast<std::byte*>(
                static_cast<void*>(std::to_address(base)));
            if (raw_base == nullptr)
            {
                throw std::bad_alloc{};
            }

            const auto user_address = align_up_constant(
                reinterpret_cast<std::uintptr_t>(raw_base) + direct_header_size,
                effective_alignment);
            auto* user = reinterpret_cast<std::byte*>(user_address);
            auto* fields = reinterpret_cast<direct_header_fields*>(
                user - direct_header_size);

            ::new (static_cast<void*>(fields)) direct_header_fields{};

            fields->base = base;
            fields->total = total;
            fields->alignment = effective_alignment;
            fields->prev = direct_header_pointer{};
            fields->next = direct_blocks_;

            if (std::to_address(direct_blocks_) != nullptr)
            {
                std::to_address(direct_blocks_)->prev = make_direct_pointer(*fields);
            }

            direct_blocks_ = make_direct_pointer(*fields);
            store_tag(user - 8, direct_block_bit);
            return make_void_pointer(user);
        }

        static chunk_pointer make_chunk_pointer(chunk_header& header) noexcept
        {
            if constexpr (std::is_constructible_v<chunk_pointer, chunk_header*>)
            {
                return static_cast<chunk_pointer>(&header);
            }
            else
            {
                return std::pointer_traits<chunk_pointer>::pointer_to(header);
            }
        }

        static direct_header_pointer make_direct_pointer(
            direct_header_fields& fields) noexcept
        {
            if constexpr (
                std::is_constructible_v<direct_header_pointer, direct_header_fields*>)
            {
                return static_cast<direct_header_pointer>(&fields);
            }
            else
            {
                return std::pointer_traits<direct_header_pointer>::pointer_to(fields);
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
    };

    static_assert(memory_resource<monotonic_buffer_resource<>>);
    static_assert(memory_resource<lifo_pool_resource<>>);
}