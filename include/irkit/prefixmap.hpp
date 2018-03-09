// MIT License
//
// Copyright (c) 2018 Michal Siedlaczek
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//! \file prefixmap.hpp
//! \author Michal Siedlaczek
//! \copyright MIT License

#pragma once

#include <boost/dynamic_bitset.hpp>
#include <boost/filesystem.hpp>
#include <cstring>
#include <gsl/gsl>
#include <iostream>
#include <list>
#include <sstream>
#include <vector>
#include "irkit/bitptr.hpp"
#include "irkit/coding/hutucker.hpp"
//#include "irkit/mutablebittrie.hpp"
#include "irkit/radix_tree.hpp"
#include "irkit/utils.hpp"

namespace irk {

namespace fs = boost::filesystem;

//! A string-based prefix map implementation.
/*!
     \tparam Index        the integral type of element indices
     \tparam MemoryBuffer the memory buffer type, e.g., `std::vector<char>` (for
                          building/reading) or `boost::iostreams::mapped_file`
                          (for reading)

     This represents a string-based prefix map, which allows for the following:
      - determinig whether a string is indexed in the map;
      - returning a string's ID (lexicographical position) if it exists;
      - returning the n-th string in lexicographical order;
      - [TODO: intervals]

      \author Michal Siedlaczek
 */
template<class Index, class MemoryBuffer, class Counter = std::uint32_t>
class prefix_map {
    static constexpr std::size_t block_data_offset =
        sizeof(Index) + sizeof(Counter);

public:
    /*!
     * An object of this class is responsible for building a single compressed
     * block of a prefix map.
     */
    class block_builder {
    private:
        Index first_index_;
        Counter count_;
        std::size_t pos_;
        std::string last_;
        std::size_t size_;
        char* block_begin_;
        irk::bitptr<char> bitp_;
        const std::shared_ptr<coding::hutucker_codec<char>> codec_;

        void encode_unary(std::uint32_t n)
        {
            for (std::uint32_t idx = 0; idx < n; ++idx) {
                bitp_.set(1);
                ++bitp_;
            }
            bitp_.set(0);
            ++bitp_;
            pos_ += n + 1;
        }

        bool can_encode(std::uint32_t length) const
        {
            return pos_ + length <= size_ * 8;
        }

    public:
        block_builder(Index first_index,
            char* block_begin,
            std::size_t size,
            const std::shared_ptr<irk::coding::hutucker_codec<char>> codec)
            : first_index_(first_index),
              count_(0),
              pos_(block_data_offset * 8),
              last_(""),
              size_(size),
              block_begin_(block_begin),
              bitp_(block_begin + block_data_offset),
              codec_(codec)
        {
            std::memcpy(block_begin, &first_index, sizeof(Index));
        }

        //block_builder(block_builder&& other)
        //    : first_index_(other.first_index_),
        //      count_(other.count_),
        //      pos_(other.pos_),
        //      last_(other.last_),
        //      size_(other.size_),
        //      block_begin_(other.block_begin_),
        //      bitp_(other.bitp_),
        //      codec_(other.codec_)
        //{}

        bool add(const std::string& value)
        {
            if (value == "zzzx2e00000000000000ff00000000zp7zh3zp3zts1113zzac000zzyn4zzzrzzz") {
                std::cout << "encoding " << value << std::endl;
                std::cout << "last.size(): " << last_.size() << std::endl;
            }

            if (value == "0000000000ef00000000000000f000000000000000f100000000000000f2") {
                std::cout << "encoding... block size: " << size_ << std::endl;
                std::cout << "last: " << last_ << std::endl;
            }
            std::uint32_t pos = 0;
            for (; pos < last_.size(); ++pos) {
                if (last_[pos] != value[pos]) {
                    break;
                }
            }
            auto encoded = codec_->encode(value.begin() + pos, value.end());
            if (!can_encode(value.size() + 2 + encoded.size())) {
                return false;
            }
            encode_unary(pos);
            encode_unary(value.size() - pos);
            if (value == "0000000000ef00000000000000f000000000000000f100000000000000f2") {
                std::cout << "pref: " << pos << std::endl;
                std::cout << "suf: " << value.size() - pos << std::endl;
                std::cout << "encoded size: " << encoded.size() << std::endl;
            }
            irk::bitcpy(bitp_, encoded);
            bitp_ += encoded.size();
            pos_ += encoded.size();
            count_++;
            last_ = value;
            return true;
        }

        void expand_by(std::size_t nbytes) { size_ += nbytes; }

        void reset(char* new_begin)
        {
            block_begin_ = new_begin;
            bitp_ = bitptr(block_begin_ + block_data_offset);
        }

        auto size() const { return size_; }

        void write_count()
        {
            std::memcpy(block_begin_ + sizeof(Index), &count_, sizeof(Counter));
        }

        void close() { pos_ = size_ * 8; }
    };

    class block_ptr {
    private:
        const char* block_begin;
        bitptr<const char> current;
        const std::shared_ptr<irk::coding::hutucker_codec<char>> codec_;
        std::string last_value;

    public:
        block_ptr(const char* block_ptr,
            const std::shared_ptr<irk::coding::hutucker_codec<char>> codec)
            : block_begin(block_ptr),
              current(block_ptr + block_data_offset),
              codec_(codec),
              last_value()
        {}

        Index first_index() const
        {
            return *reinterpret_cast<const Index*>(block_begin);
        }

        Counter count() const
        {
            return *reinterpret_cast<const Counter*>(
                block_begin + sizeof(Index));
        }

        std::size_t read_unary()
        {
            std::size_t val = 0;
            while (*current) {
                ++val;
                ++current;
            }
            ++current;
            return val;
        }

        std::string next()
        {
            std::size_t common_prefix_len = read_unary();
            std::size_t suffix_len = read_unary();
            std::cout << "pref: " << common_prefix_len << std::endl;
            std::cout << "suf: " << suffix_len << std::endl;
            last_value.resize(common_prefix_len);
            std::ostringstream o(std::move(last_value), std::ios_base::ate);
            auto reader = current.reader();
            codec_->decode(reader, o, suffix_len);
            last_value = o.str();
            return last_value;
        }
    };

private:
    MemoryBuffer blocks_;
    std::size_t block_size_;
    std::size_t block_count_;
    const std::shared_ptr<coding::hutucker_codec<char>> codec_;
    std::shared_ptr<radix_tree<Index>> block_leaders_;

    //! Append another block
    std::shared_ptr<block_builder>
    append_block(Index index, block_builder* old_builder)
    {
        if (old_builder != nullptr) {
            old_builder->write_count();
        }
        blocks_.resize(blocks_.size() + block_size_);
        ++block_count_;
        char* new_block_begin = blocks_.data() + blocks_.size() - block_size_;
        return std::make_shared<block_builder>(
            index, new_block_begin, block_size_, codec_);
    }

    void expand_block(block_builder* block)
    {
        blocks_.resize(blocks_.size() + block_size_);
        ++block_count_;
        block->expand_by(block_size_);
        char* new_begin_ptr = blocks_.data() + blocks_.size() - block->size();
        block->reset(new_begin_ptr);
    }

    std::ostream& dump_coding_tree(std::ostream& out) const
    {
        auto coding_tree = codec_->tree();
        auto mem = coding_tree.memory_container();
        std::size_t tree_size = mem.size();
        out.write(reinterpret_cast<char*>(&tree_size), sizeof(tree_size));
        out.write(mem.data(), tree_size);
        return out;
    }

    std::ostream& dump_blocks(std::ostream& out) const
    {
        std::size_t blocks_size = blocks_.size();
        out.write(reinterpret_cast<char*>(&blocks_size), sizeof(blocks_size));
        out.write(blocks_.data(), blocks_size);
        return out;
    }

    std::ostream& dump_leaders(std::ostream& out) const
    {
        raxIterator iter;
        raxStart(&iter, block_leaders_->c_rax());
        int result = raxSeek(&iter, "^", NULL, 0);
        if (result == 0) {
            throw std::bad_alloc();
        }
        std::string last;
        //std::vector<char> data(block_size_);
        std::vector<Index> values;
        std::vector<std::string> keys;
        //block_builder leader_block(Index(0), data.data(), block_size_, codec_);
        //std::size_t blocks = 1;
        while(raxNext(&iter)) {
            std::string key(iter.key, iter.key + iter.key_len);
            //std::cout << key << std::endl;
            values.push_back(*reinterpret_cast<Index*>(iter.data));
            if (values.back() == 12) std::cout << key << std::endl;
            keys.push_back(std::move(key));
            if (values.back() == 12) std::cout << keys.back() << std::endl;
            //while (!leader_block.add(key)) {
            //    data.resize(data.size() + block_size_);
            //    leader_block.expand_by(block_size_);
            //    char* new_begin_ptr = data.data();
            //    leader_block.reset(new_begin_ptr);
            //    blocks++;
            //}
        }
        //for (gsl::index idx = 0; idx < block_size_; ++idx) {
        //    std::cout << (int)data[idx] << " ";
        //}
        //std::cout << std::endl;
        //leader_block.write_count();
        auto num_values = values.size();
        std::cout << "[w] num_values: " << num_values << std::endl;
        out.write(reinterpret_cast<char*>(&num_values), sizeof(num_values));
        out.write(reinterpret_cast<char*>(values.data()),
            values.size() * sizeof(Index));
        for (const std::string& key : keys) {
            out << key << '\n';
        }
        //out.write(reinterpret_cast<char*>(&blocks), sizeof(blocks));
        //std::cout << "[w] num_blocks: " << blocks << std::endl;
        //out.write(data.data(), data.size());
        raxStop(&iter);
        return out;
    }

public:
    prefix_map(MemoryBuffer blocks,
        std::size_t block_size,
        std::size_t block_count,
        const std::shared_ptr<irk::coding::hutucker_codec<char>> codec,
        std::shared_ptr<radix_tree<Index>> block_leaders)
        : blocks_(blocks),
          block_size_(block_size),
          block_count_(block_count),
          codec_(codec),
          block_leaders_(block_leaders)
    {}

    // TODO: get rid of duplication -- leaving for testing now
    prefix_map(fs::path file,
        const std::shared_ptr<irk::coding::hutucker_codec<char>> codec,
        std::size_t block_size = 1024)
        : block_size_(block_size),
          block_count_(0),
          codec_(codec),
          block_leaders_(new radix_tree<Index>())
    {
        std::ifstream in(file.c_str());
        Index index(0);
        std::string item;
        if (!std::getline(in, item)) {
            throw std::invalid_argument("prefix map cannot be empty");
        }
        block_leaders_->insert(item, index);
        auto current_block = append_block(index, nullptr);
        if (!current_block->add(item)) {
            throw std::runtime_error("TODO: first item too long; feature pending");
        }

        while (std::getline(in, item)) {
            if (!current_block->add(item)) {
                block_leaders_->insert(item, block_count_);
                current_block = append_block(index, current_block.get());
                if (!current_block->add(item)) {
                    while (!current_block->add(item)) {
                        expand_block(current_block.get());
                    }
                    current_block->close();
                }
            }
            ++index;
        }
        current_block->write_count();
        in.close();
    }

    template<class StringRange>
    prefix_map(const StringRange& items,
        const std::shared_ptr<irk::coding::hutucker_codec<char>> codec,
        std::size_t block_size = 1024)
        : block_size_(block_size),
          block_count_(0),
          codec_(codec),
          block_leaders_(new radix_tree<Index>())
    {
        auto it = items.cbegin();
        auto last = items.cend();
        if (it == last) {
            throw std::invalid_argument("prefix map cannot be empty");
        }

        Index index(0);
        std::string item(it->begin(), it->end());
        block_leaders_->insert(item, 0);
        auto current_block = append_block(index, nullptr);

        for (; it != last; ++it) {
            std::string item(it->begin(), it->end());
            if (!current_block->add(item)) {
                block_leaders_->insert(item, block_count_);
                current_block = append_block(index, current_block.get());
                if (!current_block->add(item)) {
                    while (!current_block->add(item)) {
                        expand_block(current_block.get());
                    }
                    current_block->close();
                }
            }
            ++index;
        }
        current_block->write_count();
    }

    std::optional<Index> operator[](const std::string& key) const
    {
        auto block_opt = block_leaders_->seek_le(key);
        if (!block_opt.has_value()) {
            std::cout << "node not found" << std::endl;
            return std::nullopt;
        }
        std::size_t block_number = block_opt.value();
        std::cout << "block: " << block_number << std::endl;
        block_ptr block{blocks_.data() + block_number * block_size_, codec_};
        Index idx = block.first_index();
        std::string v = block.next();
        std::cout << "key: " << key << std::endl;
        std::cout << "v: " << v << std::endl;
        std::uint32_t c = 1;
        while (c < block.count() && v < key) {
            v = block.next();
            ++idx;
            ++c;
        }
        return v == key ? std::make_optional(idx) : std::nullopt;
    }

    std::ostream& dump(std::ostream& out) const
    {
        out.write(
            reinterpret_cast<const char*>(&block_size_), sizeof(block_size_));
        out.write(
            reinterpret_cast<const char*>(&block_count_), sizeof(block_count_));
        dump_coding_tree(out);
        //block_leaders_.dump(out);
        dump_leaders(out);
        dump_blocks(out);
        return out;
    }
};

template<class Index>
prefix_map<Index, std::vector<char>>
build_prefix_map_from_file(fs::path file, std::size_t buffer_size = 1024)
{
    std::ifstream in(file.c_str());
    std::vector<std::size_t> frequencies(256, 0);
    std::string item;
    while (std::getline(in, item)) {
        for (const char& ch : item) {
            ++frequencies[static_cast<unsigned char>(ch)];
        }
    }
    in.close();
    auto codec = std::shared_ptr<irk::coding::hutucker_codec<char>>(
        new irk::coding::hutucker_codec<char>(frequencies));
    return prefix_map<Index, std::vector<char>>(file, codec, buffer_size);
}

template<class Index, class StringRange>
prefix_map<Index, std::vector<char>>
build_prefix_map(const StringRange& items, std::size_t buffer_size = 1024)
{
    std::vector<std::size_t> frequencies(256, 0);
    for (const std::string& item : items) {
        assert(item.size() > 0);
        for (const char& ch : item) {
            ++frequencies[ch];
        }
    }
    auto codec = std::shared_ptr<irk::coding::hutucker_codec<char>>(
        new irk::coding::hutucker_codec<char>(frequencies));
    return prefix_map<Index, std::vector<char>>(items, codec, buffer_size);
}

template<class Index>
std::shared_ptr<radix_tree<Index>> load_radix_tree(std::istream& in,
    std::size_t block_size,
    std::shared_ptr<irk::coding::hutucker_codec<char>> codec)
{
    std::shared_ptr<radix_tree<Index>> rt(new radix_tree<Index>());
    std::size_t num_blocks, num_values;

    in.read(reinterpret_cast<char*>(&num_values), sizeof(num_values));
    std::cout << "[r] num_values: " << num_values << std::endl;
    std::vector<Index> values(num_values);
    in.read(reinterpret_cast<char*>(values.data()),
        num_values * sizeof(Index));

    std::string key;
    for (std::size_t idx = 0; idx < num_values; idx++) {
        std::getline(in, key);
        if (idx == 12) std::cout << key << " -> " << values[idx] << std::endl;
        rt->insert(key, values[idx]);
    }

    //in.read(reinterpret_cast<char*>(&num_blocks), sizeof(num_blocks));
    //std::cout << "[r] num_blocks: " << num_blocks << std::endl;
    //auto block_data_size = block_size * num_blocks;
    //std::vector<char> leader_block(block_data_size);
    //in.read(leader_block.data(), block_data_size);
    //std::cout << "leader_block.size(): " << leader_block.size() << std::endl;
    //typename prefix_map<Index, std::vector<char>>::block_ptr lbp{
    //    leader_block.data(), codec};
    //std::string key;
    //std::uint32_t c = 0;
    //while (c < lbp.count()) {
    //    key = lbp.next();
    //    std::cout << "inserting: " << key << " -> " << values[c] << std::endl;
    //    rt->insert(key, values[c++]);
    //}
    return rt;
}

template<class Index>
prefix_map<Index, std::vector<char>> load_prefix_map(std::istream& in)
{
    std::size_t block_size, block_count, tree_size, block_data_size;

    in.read(reinterpret_cast<char*>(&block_size), sizeof(block_size));
    in.read(reinterpret_cast<char*>(&block_count), sizeof(block_count));
    in.read(reinterpret_cast<char*>(&tree_size), sizeof(tree_size));

    std::vector<char> tree_data(tree_size);
    in.read(tree_data.data(), tree_size);
    alphabetical_bst<> encoding_tree(std::move(tree_data));
    auto codec =
        std::make_shared<irk::coding::hutucker_codec<char>>(encoding_tree);

    auto block_leaders = load_radix_tree<Index>(in, block_size, codec);

    in.read(reinterpret_cast<char*>(&block_data_size), sizeof(block_data_size));
    std::vector<char> blocks(block_data_size);
    in.read(blocks.data(), block_data_size);
    return prefix_map<Index, std::vector<char>>(std::move(blocks),
        block_size,
        block_count,
        codec,
        block_leaders);
}

template<class Index>
prefix_map<Index, std::vector<char>> load_prefix_map(const std::string& file)
{
    std::ifstream in(file, std::ios::binary);
    auto map = load_prefix_map<Index>(in);
    in.close();
    return map;
}

namespace io {

    template<class Index, class MemoryBuffer, class Counter>
    void
    dump(const prefix_map<Index, MemoryBuffer, Counter>& map, fs::path file)
    {
        std::ofstream out(file.c_str(), std::ios::binary);
        map.dump(out);
        out.close();
    }
};

};  // namespace irk