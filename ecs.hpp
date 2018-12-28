/*******************************************************************************
 * This file is part of the "https://github.com/blackmatov/ecs.hpp"
 * For conditions of distribution and use, see copyright notice in LICENSE.md
 * Copyright (C) 2018 Matvey Cherevko
 ******************************************************************************/

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <tuple>
#include <memory>
#include <vector>
#include <limits>
#include <utility>
#include <iterator>
#include <exception>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <type_traits>

// -----------------------------------------------------------------------------
//
// config
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    class entity;
    class system;
    class registry;

    using family_id = std::uint16_t;
    using entity_id = std::uint32_t;

    const std::size_t entity_id_index_bits = 22;
    const std::size_t entity_id_index_mask = (1 << entity_id_index_bits) - 1;

    const std::size_t entity_id_version_bits = 10;
    const std::size_t entity_id_version_mask = (1 << entity_id_version_bits) - 1;

    static_assert(
        std::is_unsigned<family_id>::value,
        "ecs_hpp::family_id must be an unsigned integer");

    static_assert(
        std::is_unsigned<entity_id>::value,
        "ecs_hpp::entity_id must be an unsigned integer");

    static_assert(
        sizeof(entity_id) == (entity_id_index_bits + entity_id_version_bits) / 8u,
        "ecs_hpp::entity_id mismatch index and version bits");

    constexpr inline entity_id entity_id_index(entity_id id) noexcept {
        return id & entity_id_index_mask;
    }

    constexpr inline entity_id entity_id_version(entity_id id) noexcept {
        return (id >> entity_id_index_bits) & entity_id_version_mask;
    }

    constexpr inline entity_id entity_id_join(entity_id index, entity_id version) noexcept {
        return index | (version << entity_id_index_bits);
    }
}

// -----------------------------------------------------------------------------
//
// utilities
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        //
        // as_const
        //

        template < typename T >
        constexpr std::add_const_t<T>& as_const(T& t) noexcept {
            return t;
        }

        //
        // tuple_tail
        //

        namespace impl
        {
            template < typename T, typename... Ts, std::size_t... Is >
            std::tuple<Ts...> tuple_tail_impl(
                std::tuple<T, Ts...>&& t,
                std::index_sequence<Is...> iseq)
            {
                (void)iseq;
                return std::make_tuple(std::move(std::get<Is + 1u>(t))...);
            }

            template < typename T, typename... Ts, std::size_t... Is >
            std::tuple<Ts...> tuple_tail_impl(
                const std::tuple<T, Ts...>& t,
                std::index_sequence<Is...> iseq)
            {
                (void)iseq;
                return std::make_tuple(std::get<Is + 1u>(t)...);
            }
        }

        template < typename T, typename... Ts >
        std::tuple<Ts...> tuple_tail(std::tuple<T, Ts...>&& t) {
            return impl::tuple_tail_impl(
                std::move(t),
                std::make_index_sequence<sizeof...(Ts)>());
        }

        template < typename T, typename... Ts >
        std::tuple<Ts...> tuple_tail(const std::tuple<T, Ts...>& t) {
            return impl::tuple_tail_impl(
                t,
                std::make_index_sequence<sizeof...(Ts)>());
        }

        //
        // tuple_contains
        //

        template < typename V >
        bool tuple_contains(const std::tuple<>& t, const V& v) {
            (void)t;
            (void)v;
            return false;
        }

        template < typename V, typename T, typename... Ts >
        bool tuple_contains(const std::tuple<T, Ts...>& t, const V& v) {
            if ( std::get<0>(t) == v ) {
                return true;
            }
            return tuple_contains(tuple_tail(t), v);
        }
    }
}

// -----------------------------------------------------------------------------
//
// detail::type_family
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        template < typename Void = void >
        class type_family_base {
            static_assert(
                std::is_void<Void>::value,
                "unexpected internal error");
        protected:
            static family_id last_id_;
        };

        template < typename T >
        class type_family : public type_family_base<> {
        public:
            static family_id id() noexcept {
                static family_id self_id = ++last_id_;
                assert(self_id > 0u && "ecs_hpp::family_id overflow");
                return self_id;
            }
        };

        template < typename Void >
        family_id type_family_base<Void>::last_id_ = 0u;
    }
}

// -----------------------------------------------------------------------------
//
// detail::sparse_indexer
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        template < typename T
                 , bool = std::is_unsigned<T>::value && sizeof(T) <= sizeof(std::size_t) >
        struct sparse_unsigned_indexer {
            std::size_t operator()(const T v) const noexcept {
                return static_cast<std::size_t>(v);
            }
        };

        template < typename T >
        struct sparse_unsigned_indexer<T, false> {};

        template < typename T >
        struct sparse_indexer
        : public sparse_unsigned_indexer<T> {};
    }
}

// -----------------------------------------------------------------------------
//
// detail::sparse_set
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        template < typename T
                 , typename Indexer = sparse_indexer<T> >
        class sparse_set final {
        public:
            using iterator = typename std::vector<T>::iterator;
            using const_iterator = typename std::vector<T>::const_iterator;
        public:
            iterator begin() noexcept {
                return dense_.begin();
            }

            iterator end() noexcept {
                using dt = typename std::iterator_traits<iterator>::difference_type;
                return begin() + static_cast<dt>(size_);
            }

            const_iterator begin() const noexcept {
                return dense_.begin();
            }

            const_iterator end() const noexcept {
                using dt = typename std::iterator_traits<const_iterator>::difference_type;
                return begin() + static_cast<dt>(size_);
            }

            const_iterator cbegin() const noexcept {
                return dense_.cbegin();
            }

            const_iterator cend() const noexcept {
                using dt = typename std::iterator_traits<const_iterator>::difference_type;
                return cbegin() + static_cast<dt>(size_);
            }
        public:
            sparse_set(const Indexer& indexer = Indexer())
            : indexer_(indexer) {}

            bool insert(T&& v) {
                if ( has(v) ) {
                    return false;
                }
                const std::size_t vi = indexer_(v);
                if ( vi >= capacity_ ) {
                    reserve(new_capacity_for_(vi + 1u));
                }
                dense_[size_] = std::move(v);
                sparse_[vi] = size_;
                ++size_;
                return true;
            }

            bool insert(const T& v) {
                if ( has(v) ) {
                    return false;
                }
                const std::size_t vi = indexer_(v);
                if ( vi >= capacity_ ) {
                    reserve(new_capacity_for_(vi + 1u));
                }
                dense_[size_] = v;
                sparse_[vi] = size_;
                ++size_;
                return true;
            }

            template < typename... Args >
            bool emplace(Args&&... args) {
                return insert(T(std::forward<Args>(args)...));
            }

            bool unordered_erase(const T& v)
                noexcept(std::is_nothrow_move_assignable<T>::value)
            {
                if ( !has(v) ) {
                    return false;
                }
                const std::size_t vi = indexer_(v);
                const std::size_t index = sparse_[vi];
                dense_[index] = std::move(dense_[size_ - 1u]);
                sparse_[indexer_(dense_[index])] = index;
                --size_;
                return true;
            }

            void clear() noexcept {
                size_ = 0u;
            }

            bool has(const T& v) const noexcept {
                const std::size_t vi = indexer_(v);
                return vi < capacity_
                    && sparse_[vi] < size_
                    && dense_[sparse_[vi]] == v;
            }

            const_iterator find(const T& v) const noexcept {
                const std::size_t vi = indexer_(v);
                return has(v)
                    ? begin() + sparse_[vi]
                    : end();
            }

            std::size_t get_index(const T& v) const {
                const auto p = find_index(v);
                if ( p.second ) {
                    return p.first;
                }
                throw std::logic_error("ecs_hpp::sparse_set(value not found)");
            }

            std::pair<std::size_t,bool> find_index(const T& v) const noexcept {
                return has(v)
                    ? std::make_pair(sparse_[indexer_(v)], true)
                    : std::make_pair(std::size_t(-1), false);
            }

            bool empty() const noexcept {
                return size_ == 0u;
            }

            void reserve(std::size_t ncapacity) {
                if ( ncapacity > capacity_ ) {
                    std::vector<T> ndense(ncapacity);
                    std::vector<std::size_t> nsparse(ncapacity);
                    std::copy(dense_.begin(), dense_.end(), ndense.begin());
                    std::copy(sparse_.begin(), sparse_.end(), nsparse.begin());
                    ndense.swap(dense_);
                    nsparse.swap(sparse_);
                    capacity_ = ncapacity;
                }
            }

            std::size_t size() const noexcept {
                return size_;
            }

            std::size_t max_size() const noexcept {
                return std::min(dense_.max_size(), sparse_.max_size());
            }

            std::size_t capacity() const noexcept {
                return capacity_;
            }
        private:
            std::size_t new_capacity_for_(std::size_t nsize) const {
                const std::size_t ms = max_size();
                if ( nsize > ms ) {
                    throw std::length_error("ecs_hpp::sparse_set");
                }
                if ( capacity_ >= ms / 2u ) {
                    return ms;
                }
                return std::max(capacity_ * 2u, nsize);
            }
        private:
            Indexer indexer_;
            std::vector<T> dense_;
            std::vector<std::size_t> sparse_;
            std::size_t size_{0u};
            std::size_t capacity_{0u};
        };
    }
}

// -----------------------------------------------------------------------------
//
// detail::sparse_map
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        template < typename K
                 , typename T
                 , typename Indexer = sparse_indexer<K> >
        class sparse_map final {
        public:
            using iterator = typename std::vector<K>::iterator;
            using const_iterator = typename std::vector<K>::const_iterator;
        public:
            iterator begin() noexcept {
                return keys_.begin();
            }

            iterator end() noexcept {
                return keys_.end();
            }

            const_iterator begin() const noexcept {
                return keys_.begin();
            }

            const_iterator end() const noexcept {
                return keys_.end();
            }

            const_iterator cbegin() const noexcept {
                return keys_.cbegin();
            }

            const_iterator cend() const noexcept {
                return keys_.cend();
            }
        public:
            sparse_map(const Indexer& indexer = Indexer())
            : keys_(indexer) {}

            bool insert(const K& k, const T& v) {
                if ( keys_.has(k) ) {
                    return false;
                }
                values_.push_back(v);
                try {
                    return keys_.insert(k);
                } catch (...) {
                    values_.pop_back();
                    throw;
                }
            }

            bool insert(const K& k, T&& v) {
                if ( keys_.has(k) ) {
                    return false;
                }
                values_.push_back(std::move(v));
                try {
                    return keys_.insert(k);
                } catch (...) {
                    values_.pop_back();
                    throw;
                }
            }

            template < typename... Args >
            bool emplace(const K& k, Args&&... args) {
                if ( keys_.has(k) ) {
                    return false;
                }
                values_.emplace_back(std::forward<Args>(args)...);
                try {
                    return keys_.insert(k);
                } catch (...) {
                    values_.pop_back();
                    throw;
                }
            }

            std::enable_if_t<
                std::is_nothrow_move_assignable<K>::value,
                bool>
            unordered_erase(const K& k) {
                if ( !keys_.has(k) ) {
                    return false;
                }
                const std::size_t index = keys_.get_index(k);
                values_[index] = std::move(values_.back());
                values_.pop_back();
                keys_.unordered_erase(k);
                return true;
            }

            void clear() noexcept {
                keys_.clear();
                values_.clear();
            }

            bool has(const K& k) const noexcept {
                return keys_.has(k);
            }

            T& get_value(const K& k) {
                return values_[keys_.get_index(k)];
            }

            const T& get_value(const K& k) const {
                return values_[keys_.get_index(k)];
            }

            T* find_value(const K& k) noexcept {
                const auto ip = keys_.find_index(k);
                return ip.second
                    ? &values_[ip.first]
                    : nullptr;
            }

            const T* find_value(const K& k) const noexcept {
                const auto ip = keys_.find_index(k);
                return ip.second
                    ? &values_[ip.first]
                    : nullptr;
            }

            bool empty() const noexcept {
                return values_.empty();
            }

            void reserve(std::size_t ncapacity) {
                keys_.reserve(ncapacity);
                values_.reserve(ncapacity);
            }

            std::size_t size() const noexcept {
                return values_.size();
            }

            std::size_t max_size() const noexcept {
                return std::min(keys_.max_size(), values_.max_size());
            }

            std::size_t capacity() const noexcept {
                return values_.capacity();
            }
        private:
            sparse_set<K, Indexer> keys_;
            std::vector<T> values_;
        };
    }
}

// -----------------------------------------------------------------------------
//
// detail::entity_id_indexer
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        struct entity_id_indexer {
            std::size_t operator()(entity_id id) const noexcept {
                return entity_id_index(id);
            }
        };
    }
}

// -----------------------------------------------------------------------------
//
// detail::component_storage
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    namespace detail
    {
        class component_storage_base {
        public:
            virtual ~component_storage_base() = default;
            virtual bool remove(entity_id id) noexcept = 0;
            virtual bool exists(entity_id id) const noexcept = 0;
        };

        template < typename T >
        class component_storage : public component_storage_base {
        public:
            component_storage(registry& owner);

            template < typename... Args >
            void assign(entity_id id, Args&&... args);
            bool remove(entity_id id) noexcept override;
            bool exists(entity_id id) const noexcept override;
            T* find(entity_id id) noexcept;
            const T* find(entity_id id) const noexcept;

            template < typename F >
            void for_each_component(F&& f) noexcept;
            template < typename F >
            void for_each_component(F&& f) const noexcept;
        private:
            registry& owner_;
            detail::sparse_map<entity_id, T, entity_id_indexer> components_;
        };

        template < typename T >
        component_storage<T>::component_storage(registry& owner)
        : owner_(owner) {}

        template < typename T >
        template < typename... Args >
        void component_storage<T>::assign(entity_id id, Args&&... args) {
            if ( !components_.emplace(id, std::forward<Args>(args)...) ) {
                components_.get_value(id) = T(std::forward<Args>(args)...);
            }
        }

        template < typename T >
        bool component_storage<T>::remove(entity_id id) noexcept {
            return components_.unordered_erase(id);
        }

        template < typename T >
        bool component_storage<T>::exists(entity_id id) const noexcept {
            return components_.has(id);
        }

        template < typename T >
        T* component_storage<T>::find(entity_id id) noexcept {
            return components_.find_value(id);
        }

        template < typename T >
        const T* component_storage<T>::find(entity_id id) const noexcept {
            return components_.find_value(id);
        }

        template < typename T >
        template < typename F >
        void component_storage<T>::for_each_component(F&& f) noexcept {
            for ( const auto id : components_ ) {
                f(entity(owner_, id), components_.get_value(id));
            }
        }

        template < typename T >
        template < typename F >
        void component_storage<T>::for_each_component(F&& f) const noexcept {
            for ( const auto id : components_ ) {
                f(entity(owner_, id), components_.get_value(id));
            }
        }
    }
}

// -----------------------------------------------------------------------------
//
// entity
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    class entity final {
    public:
        entity(registry& owner);
        entity(registry& owner, entity_id id);

        const registry& owner() const noexcept;
        entity_id id() const noexcept;

        bool destroy();
        bool is_alive() const noexcept;

        template < typename T, typename... Args >
        bool assign_component(Args&&... args);

        template < typename T >
        bool remove_component();

        template < typename T >
        bool exists_component() const noexcept;

        std::size_t remove_all_components() noexcept;

        template < typename T >
        T& get_component();

        template < typename T >
        const T& get_component() const;

        template < typename T >
        T* find_component() noexcept;

        template < typename T >
        const T* find_component() const noexcept;

        template < typename... Ts >
        std::tuple<Ts&...> get_components();
        template < typename... Ts >
        std::tuple<const Ts&...> get_components() const;

        template < typename... Ts >
        std::tuple<Ts*...> find_components() noexcept;
        template < typename... Ts >
        std::tuple<const Ts*...> find_components() const noexcept;
    private:
        registry* owner_;
        entity_id id_{0u};
    };

    bool operator==(const entity& l, const entity& r) noexcept;
    bool operator!=(const entity& l, const entity& r) noexcept;
}

namespace std
{
    template <>
    struct hash<ecs_hpp::entity>
        : std::unary_function<const ecs_hpp::entity&, std::size_t>
    {
        std::size_t operator()(const ecs_hpp::entity& ent) const noexcept {
            return std::hash<ecs_hpp::entity_id>()(ent.id());
        }
    };
}

// -----------------------------------------------------------------------------
//
// system
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    class system {
    public:
        virtual ~system() = default;
        virtual void process(registry& owner) = 0;
    };
}

// -----------------------------------------------------------------------------
//
// registry
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    class registry final {
    public:
        registry() = default;
        ~registry() noexcept = default;

        entity create_entity();
        bool destroy_entity(const entity& ent);
        bool is_entity_alive(const entity& ent) const noexcept;

        template < typename T, typename... Args >
        bool assign_component(const entity& ent, Args&&... args);

        template < typename T >
        bool remove_component(const entity& ent);

        template < typename T >
        bool exists_component(const entity& ent) const noexcept;

        std::size_t remove_all_components(const entity& ent) const noexcept;

        template < typename T >
        T& get_component(const entity& ent);
        template < typename T >
        const T& get_component(const entity& ent) const;

        template < typename T >
        T* find_component(const entity& ent) noexcept;
        template < typename T >
        const T* find_component(const entity& ent) const noexcept;

        template < typename... Ts >
        std::tuple<Ts&...> get_components(const entity& ent);
        template < typename... Ts >
        std::tuple<const Ts&...> get_components(const entity& ent) const;

        template < typename... Ts >
        std::tuple<Ts*...> find_components(const entity& ent) noexcept;
        template < typename... Ts >
        std::tuple<const Ts*...> find_components(const entity& ent) const noexcept;

        template < typename T, typename F >
        void for_each_component(F&& f);
        template < typename T, typename F >
        void for_each_component(F&& f) const;

        template < typename... Ts, typename F >
        void for_joined_components(F&& f);
        template < typename... Ts, typename F >
        void for_joined_components(F&& f) const;

        template < typename T, typename... Args >
        void add_system(Args&&... args);
        void process_systems();
    private:
        template < typename T >
        detail::component_storage<T>* find_storage_() noexcept;
        template < typename T >
        const detail::component_storage<T>* find_storage_() const noexcept;

        template < typename T >
        detail::component_storage<T>& get_or_create_storage_();

        bool is_entity_alive_impl_(const entity& ent) const noexcept;
        std::size_t remove_all_components_impl_(const entity& ent) const noexcept;

        template < typename T >
        T* find_component_impl_(const entity& ent) noexcept;
        template < typename T >
        const T* find_component_impl_(const entity& ent) const noexcept;

        template < typename T, typename... Ts, typename F, std::size_t I, std::size_t... Is >
        void for_joined_components_impl_(F&& f, std::index_sequence<I, Is...> iseq);

        template < typename T, typename... Ts, typename F, std::size_t I, std::size_t... Is >
        void for_joined_components_impl_(F&& f, std::index_sequence<I, Is...> iseq) const;

        template < typename T
                 , typename... Ts
                 , typename F
                 , typename Ss
                 , typename... Cs >
        void for_joined_components_impl_(
            const entity& e,
            const F& f,
            const Ss& ss,
            Cs&... cs);

        template < typename T
                 , typename... Ts
                 , typename F
                 , typename Ss
                 , typename... Cs >
        void for_joined_components_impl_(
            const entity& e,
            const F& f,
            const Ss& ss,
            const Cs&... cs) const;

        template < typename F, typename... Cs >
        void for_joined_components_impl_(
            const entity& e,
            const F& f,
            const std::tuple<>& ss,
            Cs&... cs);

        template < typename F, typename... Cs >
        void for_joined_components_impl_(
            const entity& e,
            const F& f,
            const std::tuple<>& ss,
            const Cs&... cs) const;
    private:
        entity_id last_entity_id_{0u};
        std::vector<entity_id> free_entity_ids_;
        detail::sparse_set<entity_id, detail::entity_id_indexer> entity_ids_;

        using storage_uptr = std::unique_ptr<detail::component_storage_base>;
        detail::sparse_map<family_id, storage_uptr> storages_;

        using system_uptr = std::unique_ptr<system>;
        std::vector<system_uptr> systems_;
    };
}

// -----------------------------------------------------------------------------
//
// entity impl
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    inline entity::entity(registry& owner)
    : owner_(&owner) {}

    inline entity::entity(registry& owner, entity_id id)
    : owner_(&owner)
    , id_(id) {}

    inline const registry& entity::owner() const noexcept {
        return *owner_;
    }

    inline entity_id entity::id() const noexcept {
        return id_;
    }

    inline bool entity::destroy() {
        return (*owner_).destroy_entity(*this);
    }

    inline bool entity::is_alive() const noexcept {
        return detail::as_const(*owner_).is_entity_alive(*this);
    }

    template < typename T, typename... Args >
    bool entity::assign_component(Args&&... args) {
        return (*owner_).assign_component<T>(
            *this,
            std::forward<Args>(args)...);
    }

    template < typename T >
    bool entity::remove_component() {
        return (*owner_).remove_component<T>(*this);
    }

    template < typename T >
    bool entity::exists_component() const noexcept {
        return detail::as_const(*owner_).exists_component<T>(*this);
    }

    inline std::size_t entity::remove_all_components() noexcept {
        return (*owner_).remove_all_components(*this);
    }

    template < typename T >
    T& entity::get_component() {
        return (*owner_).get_component<T>(*this);
    }

    template < typename T >
    const T& entity::get_component() const {
        return detail::as_const(*owner_).get_component<T>(*this);
    }

    template < typename T >
    T* entity::find_component() noexcept {
        return (*owner_).find_component<T>(*this);
    }

    template < typename T >
    const T* entity::find_component() const noexcept {
        return detail::as_const(*owner_).find_component<T>(*this);
    }

    template < typename... Ts >
    std::tuple<Ts&...> entity::get_components() {
        return (*owner_).get_components<Ts...>(*this);
    }

    template < typename... Ts >
    std::tuple<const Ts&...> entity::get_components() const {
        return detail::as_const(*owner_).get_components<Ts...>(*this);
    }

    template < typename... Ts >
    std::tuple<Ts*...> entity::find_components() noexcept {
        return (*owner_).find_components<Ts...>(*this);
    }

    template < typename... Ts >
    std::tuple<const Ts*...> entity::find_components() const noexcept {
        return detail::as_const(*owner_).find_components<Ts...>(*this);
    }

    inline bool operator==(const entity& l, const entity& r) noexcept {
        return &l.owner() == &r.owner()
            && l.id() == r.id();
    }

    inline bool operator!=(const entity& l, const entity& r) noexcept {
        return !(l == r);
    }
}

// -----------------------------------------------------------------------------
//
// registry impl
//
// -----------------------------------------------------------------------------

namespace ecs_hpp
{
    inline entity registry::create_entity() {
        if ( !free_entity_ids_.empty() ) {
            const auto free_ent_id = free_entity_ids_.back();
            const auto new_ent_id = entity_id_join(
                entity_id_index(free_ent_id),
                entity_id_version(free_ent_id) + 1u);
            auto ent = entity(*this, new_ent_id);
            entity_ids_.insert(new_ent_id);
            free_entity_ids_.pop_back();
            return ent;

        }
        if ( last_entity_id_ < entity_id_index_mask ) {
            auto ent = entity(*this, ++last_entity_id_);
            entity_ids_.insert(ent.id());
            return ent;
        }
        throw std::logic_error("ecs_hpp::registry(entity index overlow)");
    }

    inline bool registry::destroy_entity(const entity& ent) {
        remove_all_components_impl_(ent);
        if ( entity_ids_.unordered_erase(ent.id()) ) {
            free_entity_ids_.push_back(ent.id());
            return true;
        }
        return false;
    }

    inline bool registry::is_entity_alive(const entity& ent) const noexcept {
        return is_entity_alive_impl_(ent);
    }

    template < typename T, typename... Args >
    bool registry::assign_component(const entity& ent, Args&&... args) {
        if ( !is_entity_alive_impl_(ent) ) {
            return false;
        }
        get_or_create_storage_<T>().assign(
            ent.id(),
            std::forward<Args>(args)...);
        return true;
    }

    template < typename T >
    bool registry::remove_component(const entity& ent) {
        if ( !is_entity_alive_impl_(ent) ) {
            return false;
        }
        const detail::component_storage<T>* storage = find_storage_<T>();
        return storage
            ? storage->remove(ent.id())
            : false;
    }

    template < typename T >
    bool registry::exists_component(const entity& ent) const noexcept {
        if ( !is_entity_alive_impl_(ent) ) {
            return false;
        }
        const detail::component_storage<T>* storage = find_storage_<T>();
        return storage
            ? storage->exists(ent.id())
            : false;
    }

    inline std::size_t registry::remove_all_components(const entity& ent) const noexcept {
        return remove_all_components_impl_(ent);
    }

    template < typename T >
    T& registry::get_component(const entity& ent) {
        T* component = find_component_impl_<T>(ent);
        if ( component ) {
            return *component;
        }
        throw std::logic_error("ecs_hpp::registry(component not found)");
    }

    template < typename T >
    const T& registry::get_component(const entity& ent) const {
        const T* component = find_component_impl_<T>(ent);
        if ( component ) {
            return *component;
        }
        throw std::logic_error("ecs_hpp::registry(component not found)");
    }

    template < typename T >
    T* registry::find_component(const entity& ent) noexcept {
        return find_component_impl_<T>(ent);
    }

    template < typename T >
    const T* registry::find_component(const entity& ent) const noexcept {
        return find_component_impl_<T>(ent);
    }

    template < typename... Ts >
    std::tuple<Ts&...> registry::get_components(const entity& ent) {
        return std::make_tuple(std::ref(get_component<Ts>(ent))...);
    }

    template < typename... Ts >
    std::tuple<const Ts&...> registry::get_components(const entity& ent) const {
        return std::make_tuple(std::cref(get_component<Ts>(ent))...);
    }

    template < typename... Ts >
    std::tuple<Ts*...> registry::find_components(const entity& ent) noexcept {
        return std::make_tuple(find_component<Ts>(ent)...);
    }

    template < typename... Ts >
    std::tuple<const Ts*...> registry::find_components(const entity& ent) const noexcept {
        return std::make_tuple(find_component<Ts>(ent)...);
    }

    template < typename T, typename F >
    void registry::for_each_component(F&& f) {
        detail::component_storage<T>* storage = find_storage_<T>();
        if ( storage ) {
            storage->for_each_component(std::forward<F>(f));
        }
    }

    template < typename T, typename F >
    void registry::for_each_component(F&& f) const {
        const detail::component_storage<T>* storage = find_storage_<T>();
        if ( storage ) {
            storage->for_each_component(std::forward<F>(f));
        }
    }

    template < typename... Ts, typename F >
    void registry::for_joined_components(F&& f) {
        for_joined_components_impl_<Ts...>(
            std::forward<F>(f),
            std::make_index_sequence<sizeof...(Ts)>());
    }

    template < typename... Ts, typename F >
    void registry::for_joined_components(F&& f) const {
        for_joined_components_impl_<Ts...>(
            std::forward<F>(f),
            std::make_index_sequence<sizeof...(Ts)>());
    }

    template < typename T, typename... Args >
    void registry::add_system(Args&&... args) {
        systems_.emplace_back(
            std::make_unique<T>(std::forward<Args>(args)...));
    }

    inline void registry::process_systems() {
        for ( auto& s : systems_ ) {
            s->process(*this);
        }
    }

    template < typename T >
    detail::component_storage<T>* registry::find_storage_() noexcept {
        const auto family = detail::type_family<T>::id();
        using raw_storage_ptr = detail::component_storage<T>*;
        return storages_.has(family)
            ? static_cast<raw_storage_ptr>(storages_.get_value(family).get())
            : nullptr;
    }

    template < typename T >
    const detail::component_storage<T>* registry::find_storage_() const noexcept {
        const auto family = detail::type_family<T>::id();
        using raw_storage_ptr = const detail::component_storage<T>*;
        return storages_.has(family)
            ? static_cast<raw_storage_ptr>(storages_.get_value(family).get())
            : nullptr;
    }

    template < typename T >
    detail::component_storage<T>& registry::get_or_create_storage_() {
        detail::component_storage<T>* storage = find_storage_<T>();
        if ( storage ) {
            return *storage;
        }
        const auto family = detail::type_family<T>::id();
        storages_.emplace(
            family,
            std::make_unique<detail::component_storage<T>>(*this));
        return *static_cast<detail::component_storage<T>*>(
            storages_.get_value(family).get());
    }

    inline bool registry::is_entity_alive_impl_(const entity& ent) const noexcept {
        return entity_ids_.has(ent.id());
    }

    inline std::size_t registry::remove_all_components_impl_(const entity& ent) const noexcept {
        if ( !is_entity_alive_impl_(ent) ) {
            return 0u;
        }
        std::size_t removed_components = 0u;
        for ( const auto id : storages_ ) {
            if ( storages_.get_value(id)->remove(ent.id()) ) {
                ++removed_components;
            }
        }
        return removed_components;
    }

    template < typename T >
    T* registry::find_component_impl_(const entity& ent) noexcept {
        detail::component_storage<T>* storage = find_storage_<T>();
        return storage
            ? storage->find(ent.id())
            : nullptr;
    }

    template < typename T >
    const T* registry::find_component_impl_(const entity& ent) const noexcept {
        const detail::component_storage<T>* storage = find_storage_<T>();
        return storage
            ? storage->find(ent.id())
            : nullptr;
    }

    template < typename T
             , typename... Ts
             , typename F
             , std::size_t I
             , std::size_t... Is >
    void registry::for_joined_components_impl_(
        F&& f,
        std::index_sequence<I, Is...> iseq)
    {
        (void)iseq;
        const auto ss = std::make_tuple(find_storage_<Ts>()...);
        if ( !detail::tuple_contains(ss, nullptr) ) {
            for_each_component<T>([this, &f, &ss](const entity& e, T& t) {
                for_joined_components_impl_<Ts...>(e, f, ss, t);
            });
        }
    }

    template < typename T
             , typename... Ts
             , typename F
             , std::size_t I
             , std::size_t... Is >
    void registry::for_joined_components_impl_(
        F&& f,
        std::index_sequence<I, Is...> iseq) const
    {
        (void)iseq;
        const auto ss = std::make_tuple(find_storage_<Ts>()...);
        if ( !detail::tuple_contains(ss, nullptr) ) {
            for_each_component<T>([this, &f, &ss](const entity& e, const T& t) {
                detail::as_const(*this).for_joined_components_impl_<Ts...>(e, f, ss, t);
            });
        }
    }

    template < typename T
             , typename... Ts
             , typename F
             , typename Ss
             , typename... Cs >
    void registry::for_joined_components_impl_(
        const entity& e,
        const F& f,
        const Ss& ss,
        Cs&... cs)
    {
        T* c = std::get<0>(ss)->find(e.id());
        if ( c ) {
            for_joined_components_impl_<Ts...>(
                e,
                f,
                detail::tuple_tail(ss),
                cs...,
                *c);
        }
    }

    template < typename T
             , typename... Ts
             , typename F
             , typename Ss
             , typename... Cs >
    void registry::for_joined_components_impl_(
        const entity& e,
        const F& f,
        const Ss& ss,
        const Cs&... cs) const
    {
        const T* c = std::get<0>(ss)->find(e.id());
        if ( c ) {
            for_joined_components_impl_<Ts...>(
                e,
                f,
                detail::tuple_tail(ss),
                cs...,
                *c);
        }
    }

    template < typename F, typename... Cs >
    void registry::for_joined_components_impl_(
        const entity& e,
        const F& f,
        const std::tuple<>& ss,
        Cs&... cs)
    {
        (void)ss;
        f(e, cs...);
    }

    template < typename F, typename... Cs >
    void registry::for_joined_components_impl_(
        const entity& e,
        const F& f,
        const std::tuple<>& ss,
        const Cs&... cs) const
    {
        (void)ss;
        f(e, cs...);
    }
}
