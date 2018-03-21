/*

This file is part of VROOM.

Copyright (c) 2015-2018, Julien Coupey.
All rights reserved (see LICENSE).

*/

#include "cvrp.h"
#include "../../structures/vroom/input/input.h"

cvrp::cvrp(const input& input) : vrp(input) {
  for (const auto& v : _input._vehicles) {
    if (!v.has_capacity()) {
      throw custom_exception("Missing capacity for vehicle " +
                             std::to_string(v.id));
    }
  }
  for (const auto& j : _input._jobs) {
    if (!j.has_amount()) {
      throw custom_exception("Missing amount for job " + std::to_string(j.id));
    }
  }
}

solution cvrp::solve(unsigned nb_threads) const {
  std::vector<solution> tsp_sols;

  struct param {
    CLUSTERING_T type;
    INIT_T init;
    double regret_coeff;
  };

  auto start_clustering = std::chrono::high_resolution_clock::now();
  BOOST_LOG_TRIVIAL(info) << "[CVRP] Start clustering heuristic(s).";

  std::vector<param> parameters;
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NONE, 0});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NONE, 0.5});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NONE, 1});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NEAREST, 0});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NEAREST, 0.5});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::NEAREST, 1});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::HIGHER_AMOUNT, 0});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::HIGHER_AMOUNT, 0.5});
  parameters.push_back({CLUSTERING_T::PARALLEL, INIT_T::HIGHER_AMOUNT, 1});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NONE, 0});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NONE, 0.5});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NONE, 1});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NEAREST, 0});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NEAREST, 0.5});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::NEAREST, 1});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::HIGHER_AMOUNT, 0});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::HIGHER_AMOUNT, 0.5});
  parameters.push_back({CLUSTERING_T::SEQUENTIAL, INIT_T::HIGHER_AMOUNT, 1});

  std::vector<clustering> clusterings;
  for (const auto& p : parameters) {
    clusterings.emplace_back(_input, p.type, p.init, p.regret_coeff);
  }

  auto best_c =
    std::min_element(clusterings.begin(),
                     clusterings.end(),
                     [](auto& lhs, auto& rhs) {
                       return lhs.unassigned.size() < rhs.unassigned.size() or
                              (lhs.unassigned.size() ==
                                 rhs.unassigned.size() and
                               lhs.edges_cost < rhs.edges_cost);
                     });

  std::string strategy =
    (best_c->type == CLUSTERING_T::PARALLEL) ? "parallel" : "sequential";
  std::string init_str;
  switch (best_c->init) {
  case INIT_T::NONE:
    init_str = "none";
    break;
  case INIT_T::HIGHER_AMOUNT:
    init_str = "higher_amount";
    break;
  case INIT_T::NEAREST:
    init_str = "nearest";
    break;
  }
  BOOST_LOG_TRIVIAL(trace) << "Best clustering:" << strategy << ";" << init_str
                           << ";" << best_c->regret_coeff << ";"
                           << best_c->unassigned.size() << ";"
                           << best_c->edges_cost;

  auto end_clustering = std::chrono::high_resolution_clock::now();

  auto clustering_computing_time =
    std::chrono::duration_cast<std::chrono::milliseconds>(end_clustering -
                                                          start_clustering)
      .count();

  BOOST_LOG_TRIVIAL(info) << "[CVRP] Done, took " << clustering_computing_time
                          << " ms.";

  BOOST_LOG_TRIVIAL(info) << "[CVRP] Launching TSPs ";

  for (std::size_t i = 0; i < best_c->clusters.size(); ++i) {
    if (best_c->clusters[i].empty()) {
      continue;
    }

    tsp p(_input, best_c->clusters[i], i);

    tsp_sols.push_back(p.solve(1));
  }

  auto end_tsps = std::chrono::high_resolution_clock::now();
  auto tsp_computing_time =
    std::chrono::duration_cast<std::chrono::milliseconds>(end_tsps -
                                                          end_clustering)
      .count();

  BOOST_LOG_TRIVIAL(info) << "[CVRP] Done with TSPs, took "
                          << tsp_computing_time << " ms.";

  std::vector<route_t> routes;
  cost_t total_cost = 0;
  for (const auto& tsp_sol : tsp_sols) {
    routes.push_back(tsp_sol.routes[0]);
    total_cost += tsp_sol.summary.cost;
  }

  std::vector<job_t> unassigned_jobs;
  std::transform(best_c->unassigned.begin(),
                 best_c->unassigned.end(),
                 std::back_inserter(unassigned_jobs),
                 [&](auto j) { return _input._jobs[j]; });

  return solution(0, total_cost, std::move(routes), std::move(unassigned_jobs));
}