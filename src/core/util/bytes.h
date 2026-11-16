#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace m360 {

// Reads little-endian values out of a byte buffer. MUGEN files are little-endian and the 360 is big-endian, so every
// binary field goes through here: never memcpy a file's bytes into a struct.
class ByteReader {
public:
	ByteReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}
	explicit ByteReader(const std::vector<uint8_t> &bytes) : ByteReader(bytes.data(), bytes.size()) {}

	size_t size() const { return size_; }
	size_t pos() const { return pos_; }
	// False once any read has run past the end; the reads after it return 0.
	bool ok() const { return ok_; }

	void seek(size_t pos)
	{
		if (pos > size_)
			ok_ = false;
		pos_ = pos;
	}
	void skip(size_t bytes) { seek(pos_ + bytes); }

	uint8_t u8() { return has(1) ? data_[pos_++] : 0; }
	uint16_t u16()
	{
		if (!has(2))
			return 0;
		const uint16_t v = (uint16_t)(data_[pos_] | data_[pos_ + 1] << 8);
		pos_ += 2;
		return v;
	}
	int16_t s16() { return (int16_t)u16(); }
	uint32_t u32()
	{
		if (!has(4))
			return 0;
		const uint32_t v = (uint32_t)data_[pos_] | (uint32_t)data_[pos_ + 1] << 8 | (uint32_t)data_[pos_ + 2] << 16 |
			(uint32_t)data_[pos_ + 3] << 24;
		pos_ += 4;
		return v;
	}
	bool bytes(void *out, size_t count)
	{
		if (!has(count))
			return false;
		std::memcpy(out, data_ + pos_, count);
		pos_ += count;
		return true;
	}
	// The bytes from pos to pos + count, or an empty range past the end.
	const uint8_t *at(size_t pos, size_t count) const { return pos <= size_ && count <= size_ - pos ? data_ + pos : nullptr; }

private:
	bool has(size_t count)
	{
		if (!ok_ || pos_ > size_ || count > size_ - pos_) {
			ok_ = false;
			return false;
		}
		return true;
	}

	const uint8_t *data_;
	size_t size_;
	size_t pos_ = 0;
	bool ok_ = true;
};

} // namespace m360
