module;

#include <fcntl.h>
#include <sys/mman.h>

module qsefs.mmap;

import dxx.errors;

using namespace dxx::errors::literals;

namespace qsefs {

MappedFile::MappedFile(const stdfs::path& name)
    : file{ open(name.c_str(), O_RDONLY) }
    , mapping{nullptr}
{
    this->len = stdfs::file_size(name);
    void* map = mmap(
        nullptr, this->len, PROT_READ, MAP_PRIVATE, *this->file, 0
    );

    if (map == MAP_FAILED) {
        this->mapping = nullptr;
        throw "Could not mmap {}"_errno(name);
    }

    this->mapping = static_cast<u8*>(map);
} // <-- MappedFile::MappedFile(name)

MappedFile::MappedFile(MappedFile&& other)
    : file{std::move(other.file)}
    , mapping{nullptr}
    , len{other.len}
{ std::swap(this->mapping, other.mapping); }

MappedFile& MappedFile::operator=(MappedFile&& other) {
    this->reset();

    this->file = std::move(other.file);
    this->len  = other.len;
    std::swap(this->mapping, other.mapping);

    return *this;
}

MappedFile::~MappedFile() { this->reset(); }

void MappedFile::reset() {
    if (this->mapping != nullptr) {
        if (munmap(this->mapping, this->len) != 0) {
            std::println(std::cerr, "{}", "munmap failed"_errno.what());
        }
        this->mapping = nullptr;
    }
} // <-- void MappedFile::reset()

} // <-- namespace qsefs
