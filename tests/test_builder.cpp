#include <unordered_map>
#include <vector>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#define private public
#define protected public
#include "irkit/coding.hpp"
#include "irkit/coding/varbyte.hpp"
#include "irkit/coding/copy.hpp"
#include "irkit/index.hpp"
#include "irkit/index/builder.hpp"
#include "irkit/io.hpp"

namespace {

template<class Term, class TermId>
void assert_term_map(std::unordered_map<Term, TermId>& actual,
    std::unordered_map<Term, TermId> expected)
{
    std::vector<std::pair<Term, TermId>> actualv(actual.begin(), actual.end());
    std::vector<std::pair<Term, TermId>> expectedv(
        expected.begin(), expected.end());
    std::sort(actualv.begin(), actualv.end());
    std::sort(expectedv.begin(), expectedv.end());
    EXPECT_THAT(actualv, ::testing::ElementsAreArray(expectedv));
}

template<class DocFreq>
void assert_postings(std::vector<std::vector<DocFreq>>& actual,
    std::vector<std::vector<DocFreq>> expected)
{
    EXPECT_THAT(actual, ::testing::ElementsAreArray(expected));
}

template<class T>
std::ostream& to_string(std::ostream& o, std::vector<T> v)
{
    o << "[";
    for (auto& x : v) {
        o << x << ", ";
    }
    o << "]";
    return o;
}

template<class T>
void assert_vector(std::vector<T> actual, std::vector<T> expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t idx = 0; idx < actual.size(); ++idx) {
        EXPECT_EQ(actual[idx], expected[idx]);
    }
}

std::vector<char> flatten(std::vector<std::vector<char>> vectors)
{
    std::vector<char> result;
    for (std::vector<char>& vec : vectors) {
        result.insert(result.end(), vec.begin(), vec.end());
    }
    return result;
}

using namespace irk::coding;

TEST(IndexBuilder, add)
{
    irk::index_builder<int, std::string, int> builder;

    builder.add_term("a");
    builder.add_term("b");
    builder.add_term("a");
    assert_term_map(builder.term_map_, {{"a", 0}, {"b", 1}});
    assert_postings(builder.postings_, {{{0, 2}}, {{0, 1}}});

    builder.add_document();
    builder.add_term("c");
    builder.add_term("b");
    builder.add_term("b");
    assert_term_map(builder.term_map_, {{"a", 0}, {"b", 1}, {"c", 2}});
    assert_postings(builder.postings_, {{{0, 2}}, {{0, 1}, {1, 2}}, {{1, 1}}});
}

TEST(IndexBuilder, document_frequency)
{
    irk::index_builder<int, std::string, int> builder;
    builder.postings_ = {{{0, 2}}, {{0, 1}, {1, 2}}, {{1, 1}}};
    ASSERT_EQ(builder.document_frequency(0), 1);
    ASSERT_EQ(builder.document_frequency(1), 2);
    ASSERT_EQ(builder.document_frequency(2), 1);
}

TEST(IndexBuilder, sort_terms)
{
    irk::index_builder<int, std::string, int> builder;
    builder.term_map_ = {{"z", 0}, {"b", 1}, {"c", 2}};
    builder.postings_ = {{{0, 2}}, {{0, 1}, {1, 2}}, {{1, 1}}};
    builder.term_occurrences_ = {2, 3, 1};
    ASSERT_THAT(builder.sorted_terms_, std::nullopt);

    builder.sort_terms();
    assert_vector(builder.sorted_terms_.value(), {"b", "c", "z"});
    assert_term_map(builder.term_map_, {{"z", 2}, {"b", 0}, {"c", 1}});
    assert_postings(builder.postings_, {{{0, 1}, {1, 2}}, {{1, 1}}, {{0, 2}}});
    assert_vector(builder.term_occurrences_, {3, 1, 2});
}

class IndexBuilderWrite : public ::testing::Test {
protected:
    irk::index_builder<std::uint16_t, std::string, std::uint16_t, std::uint16_t>
        builder;
    virtual void SetUp()
    {
        builder.term_map_ = {{"z", 2}, {"b", 0}, {"c", 1}};
        builder.sorted_terms_ = {"b", "c", "z"};
        builder.postings_ = {{{0, 1}, {1, 2}}, {{1, 1}}, {{0, 2}}};
        builder.term_occurrences_ = {3, 1, 2};
    }
};

TEST_F(IndexBuilderWrite, write_terms)
{
    std::stringstream out;
    builder.write_terms(out);
    std::string actual = out.str();
    std::string expected = "b\nc\nz\n";
    ASSERT_EQ(actual, expected);
}

TEST_F(IndexBuilderWrite, write_document_ids)
{
    irk::coding::varbyte_codec<uint16_t> vb;
    std::stringstream out;
    std::stringstream off;
    builder.write_document_ids(out, off);
    std::string outs = out.str();
    std::string offs = off.str();
    std::vector<char> actual_out(outs.begin(), outs.end());
    std::vector<char> actual_off(offs.begin(), offs.end());
    std::vector<char> expected_out = flatten({
        encode({8, 1024, 1, 0, 1}, vb), encode({0, 1}, vb),
        encode({7, 1024, 1, 0, 1}, vb), encode({1}, vb),
        encode({7, 1024, 1, 0, 0}, vb), encode({0}, vb)
    });
    std::vector<char> expected_off = irk::build_offset_table<>({0, 8, 15}).data_;
    EXPECT_THAT(actual_out, ::testing::ElementsAreArray(expected_out));
    EXPECT_THAT(actual_off, ::testing::ElementsAreArray(expected_off));
}

TEST_F(IndexBuilderWrite, write_document_counts)
{
    irk::coding::varbyte_codec<uint16_t> vb;
    std::stringstream out;
    std::stringstream off;
    builder.write_document_counts(out, off);
    std::string outs = out.str();
    std::string offs = off.str();
    std::vector<char> actual_out(outs.begin(), outs.end());
    std::vector<char> actual_off(offs.begin(), offs.end());
    std::vector<char> expected_out = flatten({
        encode({7, 1024, 1, 0}, vb), encode({1, 2}, vb),
        encode({6, 1024, 1, 0}, vb), encode({1}, vb),
        encode({6, 1024, 1, 0}, vb), encode({2}, vb)
    });
    std::vector<char> expected_off = irk::build_offset_table<>({0, 7, 13}).data_;
    EXPECT_THAT(actual_out, ::testing::ElementsAreArray(expected_out));
    EXPECT_THAT(actual_off, ::testing::ElementsAreArray(expected_off));
}

TEST_F(IndexBuilderWrite, write_document_frequencies)
{
    std::stringstream out;
    builder.write_document_frequencies(out);
    std::string outs = out.str();
    std::vector<char> actual_out(outs.begin(), outs.end());
    irk::compact_table<std::uint16_t> dfs(actual_out);
    EXPECT_EQ(dfs.size(), 3);
    EXPECT_EQ(dfs[0], 2);
    EXPECT_EQ(dfs[1], 1);
    EXPECT_EQ(dfs[2], 1);
}

};  // namespace

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
