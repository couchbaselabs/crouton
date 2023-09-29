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
#include "Base.hh"

namespace crouton::util {

    /** A link in a doubly-linked circular list.
        Generally used as a base class of the actual list item class. 
        If it's a private base class, you'll need to declare class `LinkList` as a friend. */
    class Link {
    public:
        Link() = default;
        ~Link()                  {remove();}

        /// True if this Link is in a list.
        bool inList() const      {return _next != nullptr;}

    protected:
        /// Removes the Link from whatever list it's in.
        void remove() {
            if (_prev)
                _prev->_next = _next;
            if (_next) 
                _next->_prev = _prev;
            _prev = _next = nullptr;
        }

    private:
        friend class LinkList;

        Link(Link const&)       = default;
        void clear()            {_prev = _next = nullptr;}
        void clearHead()        {_prev = _next = this;}

        void insertAfter(Link* other) {
            remove();
            _prev = other;
            _next = other->_next;
            _prev->_next = this;
            _next->_prev = this;
        }

        Link *_prev = nullptr, *_next = nullptr;
    };


    // Base class of `LinkedList<LINK>`
    class LinkList {
    public:
        LinkList()                      {_head.clearHead();}

        LinkList(LinkList&& other) {
            if (other.empty())
                _head.clearHead();
            else
                mvHead(other);
        }

        LinkList& operator=(LinkList&& other) {
            clear();
            if (!other.empty())
                mvHead(other);
            return *this;
        }

        bool empty() const              {return _head._next == &_head;}

        Link& front()                   {assert(!empty()); return *_head._next;}
        Link& back()                    {assert(!empty()); return *_head._prev;}

        void push_front(Link& link)     {link.insertAfter(&_head);}
        void push_back(Link& link)      {link.insertAfter(_head._prev);}

        Link& pop_front() {
            assert(!empty());
            auto link = _head._next;
            link->remove();
            return *link;
        }

        void erase(Link& link)          {link.remove();}

        void clear() {
            Link* next;
            for (Link* link = _head._next; link != &_head; link = next) {
                next = link->_next;
                link->clear();
            }
            _head.clearHead();
        }

        static Link* next(Link* link)   {return link->_next;}

    protected:
        Link* _begin()                  {return _head._next;}
        Link* _end()                    {return &_head;}
        Link const* _begin() const      {return _head._next;}
        Link const* _end() const        {return &_head;}

        template <class LINK>
        static LINK& downcast(Link& link)   {return static_cast<LINK&>(link);}
        template <class LINK>
        static Link& upcast(LINK& link)   {return static_cast<Link&>(link);}

    private:
        LinkList(LinkList const&) = delete;
        LinkList& operator=(LinkList const&) = delete;

        void mvHead(LinkList& other) {
            _head = other._head;
            _head._next->_prev = _head._prev->_next = &_head;
            other._head.clearHead();
        }

        Link _head;
    };



    /** Linked list of values of class `LINK`, which must be a subclass of `Link`. */
    template <class LINK> requires std::is_base_of_v<Link,LINK>
    class LinkedList : private LinkList {
    public:
        LinkedList() = default;

        LinkedList(LinkedList&& other)  :LinkList(std::move(other)) { }

        LinkedList& operator=(LinkedList&& other) {
            LinkList::operator=(std::move(other));
            return *this;
        }

        bool empty() const              {return LinkList::empty();}

        LINK& front()                   {return downcast<LINK>(LinkList::front());}
        LINK& back()                    {return downcast<LINK>(LinkList::back());}

        void push_front(LINK& link)     {LinkList::push_front(upcast(link));}
        void push_back(LINK& link)      {LinkList::push_back(upcast(link));}

        LINK& pop_front()               {return downcast<LINK>(LinkList::pop_front());}

        void erase(LINK& link)          {LinkList::erase(upcast(link));}

        void clear()                    {LinkList::clear();}

        template <class T>
        class Iterator {
        public:
            T& operator*() const        {return LinkList::downcast<LINK>(*_link);}
            Iterator& operator++()      {_link = next(_link); return *this;}

            friend bool operator==(Iterator const& a, Iterator const& b) {
                return a._link == b._link;
            }
        private:
            friend class LinkedList;
            explicit Iterator(Link const* link) :_link(const_cast<Link*>(link)) { }
            Link* _link;
        };

        using iterator       = Iterator<LINK>;
        using const_iterator = Iterator<const LINK>;

        iterator begin()                {return iterator(_begin());}
        iterator end()                  {return iterator(_end());}
        const_iterator begin() const    {return const_iterator(_begin());}
        const_iterator end() const      {return const_iterator(_end());}
    };

}
