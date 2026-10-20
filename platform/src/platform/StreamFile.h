// Files read for streaming (the engine's streamed sources: docs/engine/streaming.md), by UTF-8 path: in spans
// aligned on kAlignment, into aligned buffers, past the system's file cache where it allows that (the
// stream cache's pool is the cache: the system's copy of the same pages would only push other programs
// out of memory). Windows: FILE_FLAG_NO_BUFFERING; Linux: O_DIRECT where the file system takes it, else
// read-ahead hints (posix_fadvise); macOS: F_NOCACHE and F_RDADVISE.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

namespace sub::platform {

class StreamFile {
public:
    // Reads' offsets, sizes and buffers are multiples of this: a memory page, and a multiple of a disk's
    // sector on every disk SUBstation meets (512 bytes or 4 KiB).
    static constexpr size_t kAlignment = 4096;

    StreamFile() noexcept = default;
    ~StreamFile();
    StreamFile(const StreamFile&) = delete;
    StreamFile& operator=(const StreamFile&) = delete;

    // Opens the file to read; false if it can't be (it is closed then). Unbuffered where the system and
    // the file system allow it, else through the system's cache.
    bool open(const std::string& path);
    void close() noexcept;
    bool isOpen() const noexcept;
    // Its size in bytes, when it was opened.
    uint64_t size() const noexcept { return size_; }
    // Whether its reads go past the system's file cache.
    bool unbuffered() const noexcept { return unbuffered_; }

    // Reads `bytes` from `offset` into `buffer`, all three multiples of kAlignment (an AlignedBuffer's
    // memory). Returns the bytes read: fewer at the end of the file (0 past it), -1 if the read failed
    // (the disk, or the file gone). From any thread, several at once.
    int64_t read(uint64_t offset, void* buffer, size_t bytes) noexcept;
    // [offset, offset + bytes) will be read soon: the system may read it ahead (buffered files only).
    void willNeed(uint64_t offset, uint64_t bytes) noexcept;

private:
    bool openAs(const std::string& path, bool unbuffered);

#ifdef _WIN32
    void* handle_ = nullptr;  // a HANDLE; null: closed
#else
    int fd_ = -1;
#endif
    uint64_t size_ = 0;
    bool unbuffered_ = false;
};

// Memory aligned on StreamFile::kAlignment, for its reads; its size rounded up to a multiple of it.
class AlignedBuffer {
public:
    AlignedBuffer() noexcept = default;
    explicit AlignedBuffer(size_t bytes)
        : size_((bytes + StreamFile::kAlignment - 1) / StreamFile::kAlignment * StreamFile::kAlignment),
          data_(size_ ? ::operator new(size_, std::align_val_t{StreamFile::kAlignment}) : nullptr) {}
    ~AlignedBuffer() { reset(); }
    AlignedBuffer(AlignedBuffer&& other) noexcept : size_(other.size_), data_(other.data_) {
        other.size_ = 0;
        other.data_ = nullptr;
    }
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            reset();
            size_ = other.size_;
            data_ = other.data_;
            other.size_ = 0;
            other.data_ = nullptr;
        }
        return *this;
    }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    void* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }

private:
    void reset() noexcept {
        if (data_) ::operator delete(data_, std::align_val_t{StreamFile::kAlignment});
        data_ = nullptr;
        size_ = 0;
    }
    size_t size_ = 0;
    void* data_ = nullptr;
};

}  // namespace sub::platform
