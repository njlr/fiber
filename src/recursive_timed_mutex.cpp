
//          Copyright Oliver Kowalke 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "boost/fiber/recursive_timed_mutex.hpp"

#include <algorithm>

#include <boost/assert.hpp>

#include "boost/fiber/detail/main_notifier.hpp"
#include "boost/fiber/detail/scheduler.hpp"
#include "boost/fiber/interruption.hpp"
#include "boost/fiber/operations.hpp"

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace fibers {

recursive_timed_mutex::recursive_timed_mutex() :
    owner_(),
    count_( 0),
	state_( UNLOCKED),
    splk_(),
    waiting_()
{}

recursive_timed_mutex::~recursive_timed_mutex()
{
    BOOST_ASSERT( ! owner_);
    BOOST_ASSERT( 0 == count_);
    BOOST_ASSERT( waiting_.empty() );
}

void
recursive_timed_mutex::lock()
{
    if ( LOCKED == state_ && this_fiber::get_id() == owner_)
    {
        ++count_;
        return;
    }

    state_t expected = UNLOCKED;
    while ( ! state_.compare_exchange_strong( expected, LOCKED) )
    {
        expected = UNLOCKED;
        detail::notify::ptr_t n( detail::scheduler::instance()->active() );
        if ( n)
        {
            unique_lock< detail::spinlock > lk( splk_);
            // store this fiber in order to be notified later
            waiting_.push_back( n);
            splk_.unlock();

            // suspend this fiber
            detail::scheduler::instance()->wait();
        }
        else
        {
            // notifier for main-fiber
            detail::main_notifier mn;
            n = detail::main_notifier::make_pointer( mn);

            unique_lock< detail::spinlock > lk( splk_);
            // store this fiber in order to be notified later
            waiting_.push_back( n);
            splk_.unlock();

            // wait until main-fiber gets notified
            while ( ! n->is_ready() )
            {
                // run scheduler
                detail::scheduler::instance()->run();
            }
        }
    }

    BOOST_ASSERT( ! owner_);
    BOOST_ASSERT( 0 == count_);

    owner_ = this_fiber::get_id();
    ++count_;
}

bool
recursive_timed_mutex::try_lock()
{
    if ( LOCKED == state_ && this_fiber::get_id() == owner_)
    {
        ++count_;
        return true;
    }

    state_t expected = UNLOCKED;
    if ( ! state_.compare_exchange_strong( expected, LOCKED) )
    {
        // let other fiber release the lock
        detail::scheduler::instance()->yield();
        return false;
    }

    owner_ = this_fiber::get_id();
    ++count_;

    return true;
}

bool
recursive_timed_mutex::try_lock_until( clock_type::time_point const& timeout_time)
{
    if ( LOCKED == state_ && this_fiber::get_id() == owner_)
    {
        ++count_;
        return true;
    }

    state_t expected = UNLOCKED;
    while ( clock_type::now() < timeout_time && ! state_.compare_exchange_strong( expected, LOCKED) )
    {
        expected = UNLOCKED;
        detail::notify::ptr_t n( detail::scheduler::instance()->active() );
        try
        {
            if ( n)
            {
                unique_lock< detail::spinlock > lk( splk_);
                // store this fiber in order to be notified later
                waiting_.push_back( n);
                splk_.unlock();

                // suspend this fiber until notified or timed-out
                if ( ! detail::scheduler::instance()->wait_until( timeout_time) ) {
                    unique_lock< detail::spinlock > lk( splk_);
                    // remove fiber from waiting-list
                    waiting_.erase(
                        std::find( waiting_.begin(), waiting_.end(), n) );
                    splk_.unlock();
                    return false;
                }
            }
            else
            {
                // notifier for main-fiber
                detail::main_notifier mn;
                n = detail::main_notifier::make_pointer( mn);

                unique_lock< detail::spinlock > lk( splk_);
                // store this fiber in order to be notified later
                waiting_.push_back( n);
                splk_.unlock();

                // wait until main-fiber gets notified
                while ( ! n->is_ready() )
                {
                    if ( ! ( clock_type::now() < timeout_time) )
                    {
                        unique_lock< detail::spinlock > lk( splk_);
                        // remove fiber from waiting-list
                        waiting_.erase(
                            std::find( waiting_.begin(), waiting_.end(), n) );
                        splk_.unlock();
                        return false;
                    }
                    // run scheduler
                    detail::scheduler::instance()->run();
                }
            }
        }
        catch (...)
        {
            unique_lock< detail::spinlock > lk( splk_);
            // remove fiber from waiting-list
            waiting_.erase(
                    std::find( waiting_.begin(), waiting_.end(), n) );
            splk_.unlock();
            throw;
        }
    }

    if ( LOCKED != state_) return false;

    BOOST_ASSERT( ! owner_);
    BOOST_ASSERT( 0 == count_);

    owner_ = this_fiber::get_id();
    ++count_;

    return true;
}

void
recursive_timed_mutex::unlock()
{
    BOOST_ASSERT( LOCKED == state_);
    BOOST_ASSERT( this_fiber::get_id() == owner_);
    
    if ( 0 == --count_)
    {
        detail::notify::ptr_t n;

        unique_lock< detail::spinlock > lk( splk_);
        if ( ! waiting_.empty() ) {
            n.swap( waiting_.front() );
            waiting_.pop_front();
        }
        splk_.unlock();

        owner_ = detail::fiber_base::id();
        state_ = UNLOCKED;

        if ( n)
            n->set_ready();
    }
}

}}

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_SUFFIX
#endif
