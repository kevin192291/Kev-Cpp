---
order: 30
description: A class whose data is shared between threads, guarded by a mutex with lock_guard and scoped_lock
includes: <mutex>
---
// Every read and write of balance_ happens with mutex_ locked. A lock_guard unlocks when it goes out of
// scope, even if an exception is thrown; std::scoped_lock locks several mutexes without deadlocking.
class Account
{
public:
    void deposit(long long amount)
    {
        std::lock_guard lock(mutex_);
        balance_ += amount;
    }

    bool withdraw(long long amount)
    {
        std::lock_guard lock(mutex_);
        if (amount > balance_)
        {
            return false;
        }
        balance_ -= amount;
        return true;
    }

    long long balance() const
    {
        std::lock_guard lock(mutex_);
        return balance_;
    }

    friend bool transfer(Account& from, Account& to, long long amount)
    {
        if (&from == &to)
        {
            return true;
        }
        std::scoped_lock lock(from.mutex_, to.mutex_); // both at once, in a deadlock-free order
        if (amount > from.balance_)
        {
            return false;
        }
        from.balance_ -= amount;
        to.balance_ += amount;
        return true;
    }

private:
    mutable std::mutex mutex_; // mutable, so const functions can lock it too
    long long balance_ = 0;
};
