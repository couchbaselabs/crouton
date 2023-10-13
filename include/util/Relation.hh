//
// Relation.hh
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "util/LinkedList.hh"

namespace crouton::util {
    template <class Self, class Other> class ToOne;


    // A data member of Self that points back to its containing object.
    template <class Self>
    class Child {
    public:
        explicit Child(Self* self) noexcept
        :_selfOffset(unsigned(uintptr_t(this) - uintptr_t(self)))
        {
            assert((void*)this >= self && (void*)(this + 1) <= self + 1);
        }

        Self* self() noexcept pure   {return (Self*)(uintptr_t(this) - _selfOffset);}

    private:
        unsigned const  _selfOffset;        // My byte offset within my Self instance
    };


#pragma mark - ONE TO ONE:


    /** A bidirectional one-to-one relation between an instance of Self and an instance of Other.
        - If one object's link to the other is changed or cleared, the other's is cleared.
        - If one object is moved, the other will point to the new address.
        - If one object is destructed, the other will point to nullptr.
        @note The OneToOne object must be a data member of Self.
        @warning  Not thread-safe. */
    template <class Self, class Other>
    class OneToOne : private Child<Self> {
    public:
        /// Initializes an unconnected OneToOne. This should be a member initializer of Self.
        explicit OneToOne(Self* self) noexcept   :Child<Self>(self) { }

        /// Initializes a connected OneToOne. This should be a member initializer of Self.
        OneToOne(Self* self, OneToOne<Other,Self>* other) noexcept
        :OneToOne(self)
        {
            _other = other;
            hookup();
        }

        OneToOne(OneToOne&& old) noexcept
        :Child<Self>(old)
        ,_other(old._other)
        {
            old._other = nullptr;
            hookup();
        }

        OneToOne& operator=(OneToOne&& old) noexcept {
            _other = old._other;
            old._other = nullptr;
            hookup();
        }

        /// Connects to an `Other` object, or none. Breaks any existing link.
        OneToOne& operator= (OneToOne<Other,Self>* b) noexcept {
            if (b != _other) {
                unhook();
                _other = b;
                hookup();
            }
            return *this;
        }

        /// A pointer to the target `Other` object. May be nullptr.
        Other* other() const noexcept pure      {return _other ? _other->self() : nullptr;}

        operator Other*() const noexcept pure   {return other();}
        Other* operator->() const noexcept pure {return other();}

        ~OneToOne() noexcept                    {unhook();}

    private:
        friend class OneToOne<Other,Self>;

        OneToOne(OneToOne const&) = delete;
        OneToOne& operator=(OneToOne const&) = delete;

        void hookup() noexcept {
            if (_other)
                _other->_other = this;
        }

        void unhook() noexcept {
            if (_other) {
                assert(_other->_other == this);
                _other->_other = nullptr;
            }
        }

        OneToOne<Other,Self>* _other = nullptr;
    };


#pragma mark - TO MANY:


    /** A bidirectional one-to-many relation between an instance of Self and instances of Other.
        @note Must be a member variable of Self.
        @note Other must have a member variable of type `ToOne<Other,Self>`.
        @warning  Not thread-safe. */
    template <class Self, class Other>
    class ToMany : private LinkedList<ToOne<Other,Self>>, private Child<Self> {
    public:
        using super = LinkedList<ToOne<Other,Self>>;

        /// Initializes an unconnected Child. This should be a member initializer of Self.
        explicit ToMany(Self* self)  noexcept    :Child<Self>(self) { }

        ToMany(ToMany&& other) noexcept
        :super(std::move(other))
        {
            adopt();
        }

        ToMany& operator=(ToMany&& other) noexcept {
            if (&other != this) {
                super::operator=(std::move(other));
                adopt();
            }
            return *this;
        }

        using super::empty;

        class iterator {
        public:
            explicit iterator(typename super::iterator i) noexcept            :_i(i) { }
            Other& operator*() const noexcept pure                            {return *(_i->self());}
            Other* operator->() const noexcept pure                           {return _i->self();}
            iterator& operator++() noexcept                                   {++_i; return *this;}
            friend bool operator==(iterator const& a, iterator const& b) pure {return a._i == b._i;}
        private:
            typename super::iterator _i;
        };

        iterator begin() noexcept pure                       {return iterator(super::begin());}
        iterator end() noexcept pure                         {return iterator(super::end());}

        void push_front(ToOne<Other,Self>& link) noexcept    {super::push_front(link); link._parent = this;}
        void push_back(ToOne<Other,Self>& link) noexcept     {super::push_back(link); link._parent = this;}
        void erase(ToOne<Other,Self>& link) noexcept         {super::erase(link); link._parent = nullptr;}

        void clear() noexcept                                {deAdopt(); super::clear();}

        ~ToMany() noexcept                                   {deAdopt();}

    private:
        friend ToOne<Other,Self>;

        void adopt() noexcept {
            for (ToOne<Other,Self>& child : (super&)*this)
                child._parent = this;
        }
        void deAdopt() noexcept {
            for (ToOne<Other,Self>& child : (super&)*this)
                child._parent = nullptr;
        }
    };


#pragma mark - TO ONE:


    /** A bidirectional many-to-one relation between an instance of Self and an instance of Other.
        @note Must be a member variable of Self.
        @note Other must have a member variable of type `ToMany<Other,Self>`.
        @warning  Not thread-safe. */
    template <class Self, class Other>
    class ToOne : private Link, private Child<Self> {
    public:
        /// Initializes an unconnected instance. This should be a member initializer of Self.
        explicit ToOne(Self* self) noexcept     :Child<Self>(self) { }

        /// Initializes a connected instance. This should be a member initializer of Self.
        ToOne(Self* self, ToMany<Other,Self>* other) noexcept
        :Child<Self>(self)
        ,_parent(other)
        {
            if (_parent)
                _parent->push_back(*this);
        }

        ToOne(ToOne&& old) noexcept
        :Link(std::move(old))
        ,Child<Self>(old)
        ,_parent(old._parent)
        {
            old._parent = nullptr;
        }

        ToOne& operator=(ToOne&& old) noexcept {
            this->Link::operator=(std::move(old));
            _parent = old._parent;
            old._parent = nullptr;
        }

        /// Connects to an `Other` object, or none. Breaks any existing link.
        ToOne& operator= (ToMany<Other,Self>* parent) noexcept {
            if (parent != _parent) {
                remove();
                _parent = parent;
                if (_parent)
                    _parent->push_back(*this);
            }
            return *this;
        }

        /// A pointer to the target `Other` object. May be nullptr.
        Other* other() const noexcept pure   {return _parent ? _parent->self() : nullptr;}

    private:
        friend class ToMany<Other,Self>;
        friend class LinkList;

        ToMany<Other,Self>* _parent = nullptr;
    };

}
