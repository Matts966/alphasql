//
// Copyright 2019 Matts966
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "alphasql/dag_lib.h"
#include "boost/graph/depth_first_search.hpp"
#include "gtest/gtest.h"

namespace alphasql {
namespace {

using namespace boost;

typedef adjacency_list<vecS, vecS, directedS, property<vertex_name_t, std::string>> Graph;
bool has_cycle = false;

TEST(cycle_detector, cycle) {  
  Graph g1(2);
  add_edge(0, 1, g);
  add_edge(1, 0, g);
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  ASSERT_TRUE(has_cycle);

  has_cycle = false;
  Graph g2(3);
  add_edge(0, 1, g2);
  add_edge(1, 2, g2);
  add_edge(2, 0, g2);
  cycle_detector vis(has_cycle);
  depth_first_search(g2, visitor(vis));
  ASSERT_TRUE(has_cycle);

  has_cycle = false;
  Graph g3(5);
  add_edge(0, 1, g3);
  add_edge(1, 2, g3);
  add_edge(2, 3, g3);
  add_edge(3, 4, g3);
  add_edge(4, 0, g3);
  cycle_detector vis(has_cycle);
  depth_first_search(g3, visitor(vis));
  ASSERT_TRUE(has_cycle);
}

TEST(cycle_detector, acycle) {
  has_cycle = false;
  Graph g1(2);
  add_edge(0, 1, g);
  cycle_detector vis(has_cycle);
  depth_first_search(g, visitor(vis));
  ASSERT_FALSE(has_cycle);

  has_cycle = false;
  Graph g2(3);
  add_edge(0, 1, g2);
  add_edge(1, 2, g2);
  add_edge(0, 2, g2);
  cycle_detector vis(has_cycle);
  depth_first_search(g2, visitor(vis));
  ASSERT_FALSE(has_cycle);
}

}
