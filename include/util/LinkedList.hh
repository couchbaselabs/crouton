//
// LinkedList.hh
//
// Copyright © 2023-Present Couchbase, Inc. All rights reserved.
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
#include "util/Base.hh"

namespace crouton::util {

    /** A link in a doubly-linked circular list.
        Generally used as a base class of the actual list item class. 
        If it's a _private_ base class, you'll need to declare class `LinkList` as a friend. */
    class Link {
    public:
        Link() noexcept = default;
        ~Link() noexcept                 {remove();}

        Link(Link&& old) noexcept        {replace(std::move(old));}

        Link& operator=(Link&& old) noexcept {
            if (this != &old) {
                remove();
                replace(std::move(old));
            }
            return *this;
        }

        /// True if this Link is in a list.
        bool inList() const noexcept Pure      {return _next != nullptr;}

    protected:
        /// Removes the Link from whatever list it's in.
        void remove() noexcept {
            if (_prev)
                _prev->_next = _next;
            if (_next) 
                _next->_prev = _prev;
            _prev = _next = nullptr;
        }

    private:
        friend class LinkList;

        Link(Link const&) noexcept       = default;
        void clear() noexcept            {_prev = _next = nullptr;}
        void clearHead() noexcept        {_prev = _next = this;}

        void replace(Link&& old) noexcept {
            assert(!_prev && !_next);
            if (old._prev) {
                _prev = old._prev;
                _next = old._next;
                _prev->_next = this;
                _next->_prev = this;
                old.clear();
            }
        }

        void insertAfter(Link* other) noexcept {
            remove();
            _prev = other;
            _next = other->_next;
            _prev->_next = this;
            _next->_prev = this;
        }

        Link* _prev = nullptr;
        Link* _next = nullptr;
    };


    // Base class of `LinkedList<LINK>`
    class LinkList {
    public:
        LinkList() noexcept                      {_head.clearHead();}

        LinkList(LinkList&& other) noexcept {
            if (other.empty())
                _head.clearHead();
            else
                mvHead(other);
        }

        LinkList& operator=(LinkList&& other) noexcept {
            if (&other != this) {
                clear();
                if (!other.empty())
                    mvHead(other);
            }
            return *this;
        }

        bool empty() const noexcept Pure         {return _head._next == &_head;}

        Link& front() noexcept Pure              {assert(!empty()); return *_head._next;}
        Link& back() noexcept Pure               {assert(!empty()); return *_head._prev;}

        void push_front(Link& link) noexcept     {link.insertAfter(&_head);}
        void push_back(Link& link) noexcept      {link.insertAfter(_head._prev);}

        Link& pop_front() noexcept {
            precondition(!empty());
            auto link = _head._next;
            link->remove();
            return *link;
        }

        void erase(Link& link) noexcept          {link.remove();}

        void clear() noexcept {
            Link* next;
            for (Link* link = _head._next; link != &_head; link = next) {
                next = link->_next;
                link->clear();
            }
            _head.clearHead();
        }

        ~LinkList() noexcept                     {clear();}

        static Link* next(Link* link) noexcept   {return link->_next;}

    protected:
        Link* _begin() noexcept Pure                  {return _head._next;}
        Link* _end() noexcept Pure                    {return &_head;}
        Link const* _begin() const noexcept Pure      {return _head._next;}
        Link const* _end() const noexcept Pure        {return &_head;}

        template <class LINK>
        Pure static LINK& downcast(Link& link) noexcept {return static_cast<LINK&>(link);}
        template <class LINK>
        Pure static Link& upcast(LINK& link) noexcept   {return static_cast<Link&>(link);}

    private:
        LinkList(LinkList const&) = delete;
        LinkList& operator=(LinkList const&) = delete;

        void mvHead(LinkList& other) noexcept {
            assert(!other.empty());
            _head = std::move(other._head);
            other._head.clearHead();
        }

        Link _head;
    };



    /** Linked list of values of class `LINK`, which must be a subclass of `Link`. */
    template <class LINK> requires std::is_base_of_v<Link,LINK>
    class LinkedList : private LinkList {
    public:
        LinkedList() = default;

        LinkedList(LinkedList&& other) noexcept  :LinkList(std::move(other)) { }

        LinkedList& operator=(LinkedList&& other) noexcept {
            LinkList::operator=(std::move(other));
            return *this;
        }

        bool empty() const noexcept Pure         {return LinkList::empty();}

        LINK& front() noexcept Pure              {return downcast<LINK>(LinkList::front());}
        LINK& back() noexcept Pure               {return downcast<LINK>(LinkList::back());}

        void push_front(LINK& link) noexcept     {LinkList::push_front(upcast(link));}
        void push_back(LINK& link) noexcept      {LinkList::push_back(upcast(link));}

        LINK& pop_front() noexcept               {return downcast<LINK>(LinkList::pop_front());}

        void erase(LINK& link) noexcept          {LinkList::erase(upcast(link));}

        void clear() noexcept                    {LinkList::clear();}

        template <class T>
        class Iterator {
        public:
            T& operator*() const noexcept Pure       {return LinkList::downcast<LINK>(*_link);}
            T* operator->() const noexcept Pure      {return &LinkList::downcast<LINK>(*_link);}
            Iterator& operator++() noexcept          {_link = next(_link); return *this;}

            friend bool operator==(Iterator const& a, Iterator const& b) noexcept Pure {
                return a._link == b._link;
            }
        private:
            friend class LinkedList;
            explicit Iterator(Link const* link) noexcept :_link(const_cast<Link*>(link)) { }
            Link* _link;
        };

        using iterator       = Iterator<LINK>;
        using const_iterator = Iterator<const LINK>;

        iterator begin() noexcept Pure                {return iterator(_begin());}
        iterator end() noexcept Pure                  {return iterator(_end());}
        const_iterator begin() const noexcept Pure    {return const_iterator(_begin());}
        const_iterator end() const noexcept Pure      {return const_iterator(_end());}
    };

}
