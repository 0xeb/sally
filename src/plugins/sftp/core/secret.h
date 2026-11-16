// Byte buffer for passwords and passphrases that wipes itself.
#pragma once

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

// Owns a zero-terminated heap buffer. Every byte that stops being used (clear, assign, shrink, grow-by-reallocation,
// destructor) is overwritten with SecureZeroMemory first. Copying is deleted so a secret is never duplicated by
// accident; use Clone() when a second copy is really needed. Moving transfers the buffer (the source becomes empty).
// Note: assign(const std::string&) cannot wipe the caller's string; keep secrets in CSecret from the start.
class CSecret
{
public:
    CSecret() = default;
    explicit CSecret(const std::string& text) { assign(text.data(), text.size()); }
    explicit CSecret(const char* text) { assign(text, text != nullptr ? strlen(text) : 0); }
    CSecret(CSecret&& other) noexcept { swap(other); }
    CSecret& operator=(CSecret&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            swap(other);
        }
        return *this;
    }
    CSecret(const CSecret&) = delete;
    CSecret& operator=(const CSecret&) = delete;
    ~CSecret() { Release(); }

    CSecret Clone() const
    {
        CSecret copy;
        copy.assign(data_, size_);
        return copy;
    }

    // Replaces the content (the old content is wiped). Self-assignment from a part of this secret is not supported.
    void assign(const char* bytes, size_t length)
    {
        // the new buffer is filled before the old one is released, so a throwing allocation changes nothing
        char* fresh = Allocate(length);
        if (length > 0)
            memcpy(fresh, bytes, length);
        Release();
        data_ = fresh;
        size_ = length;
        capacity_ = length;
    }
    void assign(const std::string& text) { assign(text.data(), text.size()); }

    // Wipes the content and sets the size to 0; the buffer stays allocated (capacity() is unchanged).
    void clear()
    {
        if (data_ != nullptr)
            SecureZeroMemory(data_, capacity_ + 1);
        size_ = 0;
    }

    // New bytes are zero. Shrinking wipes the removed bytes. Growing beyond capacity() moves to a new buffer
    // and wipes the old one.
    void resize(size_t length)
    {
        if (length <= capacity_)
        {
            if (length < size_)
                SecureZeroMemory(data_ + length, size_ - length);
            size_ = length;
            return;
        }
        char* fresh = Allocate(length);
        if (size_ > 0)
            memcpy(fresh, data_, size_);
        Release();
        data_ = fresh;
        size_ = length;
        capacity_ = length;
    }

    // Usable as a buffer for APIs that fill it: resize(n), pass data(), then resize(actual length).
    char* data() { return data_ != nullptr ? data_ : Empty(); }
    const char* data() const { return data_ != nullptr ? data_ : Empty(); }
    const char* c_str() const { return data(); } // always zero-terminated
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    // Copies the content out. The returned string is NOT wiped by this class: use it only for the shortest
    // possible time (e.g. to hand it to a library call).
    std::string ToStdString() const { return std::string(data(), size_); }

private:
    static char* Allocate(size_t length) { return new char[length + 1](); } // zero-initialised, incl. terminator
    static char* Empty()
    {
        static char zero = '\0';
        return &zero;
    }
    void swap(CSecret& other) noexcept
    {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }
    void Release()
    {
        if (data_ != nullptr)
        {
            SecureZeroMemory(data_, capacity_ + 1);
            delete[] data_;
        }
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }

    char* data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};
