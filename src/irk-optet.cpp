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

//! \file
//! \author     Michal Siedlaczek
//! \copyright  MIT License

#include <chrono>
#include <iostream>

#include <CLI/CLI.hpp>
#include <CLI/Option.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <irm.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "cli.hpp"
#include <irkit/compacttable.hpp>
#include <irkit/index.hpp>
#include <irkit/index/source.hpp>
#include <irkit/parsing/stemmer.hpp>
#include <irkit/taat.hpp>

using std::uint32_t;
using irk::index::document_t;
using mapping_t = std::vector<document_t>;
using metric_t = std::function<double(const std::vector<int>&)>;

template<class Index>
inline void run_query(const Index& index,
    const boost::filesystem::path& dir,
    std::vector<std::string>& query,
    const std::vector<irm::trec_rel>& qrels,
    int k,
    bool nostem,
    const irk::cli::docmap& reordering,
    const metric_t& metric,
    const std::string& trecid,
    int eval_step)
{
    auto log = spdlog::get("stderr");
    irk::cli::stem_if(not nostem, query);

    log->info("Processing query {} ({})",
        trecid,
        fmt::join(query.begin(), query.end(), ", "));
    log->info("Running exhaustive");

    auto postings = irk::query_postings(index, query);
    std::vector<uint32_t> acc(index.collection_size(), 0);
    irk::taat(postings, acc);
    auto top_results = irk::aggregate_top_k<document_t, uint32_t>(
        std::begin(acc), std::end(acc), k);

    std::vector<irm::trec_result> trec_results;
    const auto& titles = index.titles();
    int rank = 0;
    for (auto& result : top_results) {
        auto title = titles.key_at(result.first);
        trec_results.push_back({trecid, "iter", title, rank++, 0.0, "run"});
    }
    irm::annotate_single(trec_results, qrels);
    std::vector<int> relevances;
    std::transform(std::begin(trec_results),
        std::end(trec_results),
        std::back_inserter(relevances),
        [](const auto& r) -> int { return r.relevance; });
    double actual = metric(relevances);
    log->info("Exhaustive score: {}", actual);

    std::unordered_map<std::string, int> relmap;
    for (const auto& rel : qrels) { relmap[rel.document_id] = rel.relevance; }

    /* Optimize */
    log->info("Running early termination");
    std::fill(std::begin(acc), std::end(acc), 0);
    irk::taat(postings, acc, reordering.doc2rank());
    irk::top_k_accumulator<document_t, uint32_t> top(k);
    document_t doc = 0;
    for (const auto& score : acc) {
        top.accumulate(reordering.doc(doc++), score);
        if (doc % eval_step == 0) {
            rank = 0;
            trec_results.clear();
            relevances.clear();
            std::transform(
                std::begin(top.unsorted()),
                std::end(top.unsorted()),
                std::back_inserter(trec_results),
                [&rank, &trecid, &titles](const auto& r) {
                    return irm::trec_result{trecid,
                        "iter",
                        titles.key_at(r.first),
                        rank++,
                        boost::numeric_cast<double>(r.second),
                        "run"};
                });
            std::sort(
                std::begin(trec_results),
                std::end(trec_results),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.score > rhs.score;
                });
            irm::annotate_single(trec_results, qrels);
            std::transform(
                std::begin(trec_results),
                std::end(trec_results),
                std::back_inserter(relevances),
                [](const auto& r) -> int { return r.relevance; });
            double m = metric(relevances);
            if (m > actual - 0.0001) { break; }
            log->info("Accumulated {:n} documents (score: {}; goal: {})",
                      doc, m, actual);
        }
    }
    log->info("Converged after {} documents", doc);
    std::cout << static_cast<double>(doc) / index.collection_size()
              << std::endl;
}

int main(int argc, char** argv)
{
    auto [app, args] = irk::cli::app(
        "Run early termination until the given metric converges.",
        irk::cli::index_dir_opt{},
        irk::cli::query_opt{},
        irk::cli::reordering_opt{},
        irk::cli::metric_opt{});
    std::string qrels_file;
    int eval_step = 100'000;
    app->add_option("--step", eval_step, "Evaluation step", true);
    app->add_option(
           "-q,--qrels", qrels_file, "Query relevance file in TREC format")
        ->required();
    CLI11_PARSE(*app, argc, argv);

    boost::filesystem::path dir(args->index_dir);
    auto data = irk::Inverted_Index_Mapped_Source::from(dir, {"bm25"});
    irk::inverted_index_view index(irtl::value(data));

    std::optional<irk::cli::docmap> reordering(std::nullopt);
    if (not args->reordering.empty()) {
        std::string prefix = (dir / args->reordering).string();
        reordering = std::make_optional(irk::cli::docmap::from_files(prefix));
    }

    auto qrels = irm::read_trec_rels(qrels_file);
    auto grouped_rels = group_by_query(qrels);

    auto console = spdlog::stderr_color_mt("stderr");
    if (args->read_files) {
        int current_trecid = args->trecid;
        for (const auto& file : args->terms_or_files)
        {
            std::ifstream in(file);
            std::string q;
            while(std::getline(in, q))
            {
                std::istringstream qin(q);
                std::string term;
                std::vector<std::string> terms;
                while (qin >> term) { terms.push_back(std::move(term)); }
                auto trecid = std::to_string(current_trecid++);
                run_query(index,
                    dir,
                    terms,
                    grouped_rels[trecid],
                    args->k,
                    args->nostem,
                    reordering.value(),
                    irm::parse_metric(args->metric),
                    trecid,
                    eval_step);
            }
        }
    }
    else {
        auto trecid = std::to_string(args->trecid);
        run_query(index,
            dir,
            args->terms_or_files,
            grouped_rels[trecid],
            args->k,
            args->nostem,
            reordering.value(),
            irm::parse_metric(args->metric),
            trecid,
            eval_step);
    }
}
