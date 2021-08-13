#include "baldr/rapidjson_utils.h"
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstdint>

#include "baldr/graphconstants.h"
#include "baldr/graphreader.h"
#include "baldr/tilehierarchy.h"
#include "midgard/encoded.h"
#include "midgard/logging.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <utility>

#include "config.h"

using namespace valhalla::midgard;
using namespace valhalla::baldr;

namespace bpo = boost::program_options;

// global options instead of passing them around
std::string config;

namespace {

// often we need both the edge id and the directed edge, so lets have something to represent that
struct edge_t {
  GraphId i;
  const DirectedEdge* e;
  operator const GraphId&() const {
    return i;
  }
  operator const DirectedEdge*() const {
    return e;
  }
  operator bool() const {
    return i.Is_Valid() && e;
  }
};

// Get the opposing edge - if the opposing index is invalid return a nullptr
// for the directed edge. This should not occur but this can happen in
// GraphValidator if it fails to find an opposing edge.
edge_t opposing(GraphReader& reader, graph_tile_ptr tile, const GraphId& edge_id) {
  const DirectedEdge* opp_edge = nullptr;
  auto opp_id = reader.GetOpposingEdgeId(edge_id, opp_edge, tile);
  return {opp_id, opp_edge};
}



std::tuple<uint64_t, std::list<PointLL>, std::vector<uint64_t>> extend(GraphReader& reader,
            graph_tile_ptr& tile,
            const edge_t& edge) {
  // get the shape
  if (edge.i.Tile_Base() != tile->id()) {
    tile = reader.GetGraphTile(edge.i);
  }
  // get the shape
  auto info = tile->edgeinfo(edge.e);
  auto more = valhalla::midgard::decode7<std::list<PointLL>>(info.encoded_shape());

  std::tuple<uint64_t, std::list<PointLL>, std::vector<uint64_t>> result;

  if(info.has_osmids()) {
    result = std::make_tuple(edge.i.value, more, info.osmids_vector());
  }

  return result;
}

} // namespace



// program entry point
int main(int argc, char* argv[]) {
  bpo::options_description options("valhalla_osm_network " VALHALLA_VERSION "\n"
                                   "\n"
                                   " Usage: valhalla_osm_network [options]\n"
                                   "\n"
                                   "valhalla_osm_network is a simple command line test tool which "
                                   "dumps a representation of the network with respect to OSM ids. "
                                   "\n");

  options.add_options()
      // positional arguments
      ("config", bpo::value<std::string>(&config), "Valhalla configuration file [required]");

  bpo::positional_options_description pos_options;
  pos_options.add("config", 1);
  bpo::variables_map vm;
  try {
    bpo::store(bpo::command_line_parser(argc, argv).options(options).positional(pos_options).run(),
               vm);
    bpo::notify(vm);
  } catch (std::exception& e) {
    std::cerr << "Unable to parse command line options because: " << e.what() << "\n"
              << "This is a bug, please report it at " PACKAGE_BUGREPORT << "\n";
    return EXIT_FAILURE;
  }

  if (!vm.count("config")) {
    std::cout << options << "\n";
    return EXIT_SUCCESS;
  }

  // parse the config
  boost::property_tree::ptree pt;
  rapidjson::read_json(config.c_str(), pt);

  // configure logging
  valhalla::midgard::logging::Configure({{"type", "std_err"}, {"color", "true"}});

  // get something we can use to fetch tiles
  valhalla::baldr::GraphReader reader(pt.get_child("mjolnir"));

  // keep the global number of edges encountered at the point we encounter each tile
  // this allows an edge to have a sequential global id and makes storing it very small
  LOG_INFO("Enumerating edges...");
  std::unordered_map<GraphId, uint64_t> tile_set(kMaxGraphTileId * TileHierarchy::levels().size());
  uint64_t edge_count = 0;
  for (const auto& level : TileHierarchy::levels()) {
    for (uint32_t i = 0; i < level.tiles.TileCount(); ++i) {
      GraphId tile_id{i, level.level, 0};
      if (reader.DoesTileExist(tile_id)) {
        // TODO: just read the header, parsing the whole thing isnt worth it at this point
        tile_set.emplace(tile_id, edge_count);
        auto tile = reader.GetGraphTile(tile_id);
        assert(tile);
        edge_count += tile->header()->directededgecount();
        reader.Clear();
      }
    }
  }

  // for each tile
  LOG_INFO("Exporting " + std::to_string(edge_count) + " edges");
  int ndumped = 0;

  std::ofstream edge_ids_f("edge-ids.json");
  std::ofstream osm_ids_f("osm-ids.json");
  std::ofstream ll_f("lls.json");
  std::ofstream network_f("network.json");
  std::ofstream edge_pairs_f("edge-pairs.json");

  std::vector<std::pair<uint64_t, uint64_t>> paired_edges;
  std::set<uint64_t> paired_edge_ids;

  // open the dicts and lists at the beginning of the outputs
  edge_ids_f << "[\n";
  osm_ids_f  << "[\n";
  ll_f       << "[\n" << std::setprecision(10);
  network_f  << "{\n" << std::setprecision(10);

  for (const auto& tile_count_pair : tile_set) {
    // for each edge in the tile
    reader.Clear();

    auto tile = reader.GetGraphTile(tile_count_pair.first);
    assert(tile);

    for (uint32_t i = 0; i < tile->header()->directededgecount(); ++i) {

      // make sure we dont ever look at this again
      edge_t edge{tile_count_pair.first, tile->directededge(i)};
      edge.i.set_id(i);

      // these wont have opposing edges that we care about
      if (edge.e->use() == Use::kTransitConnection ||
          edge.e->IsTransitLine()) { // these 2 should never happen
        continue;
      }

      // get the opposing edge as well (ensure a valid edge is returned)
      edge_t opposing_edge = opposing(reader, tile, edge);
      if (opposing_edge.e == nullptr) {
        continue;
      }

      // shortcuts arent real and maybe we dont want ferries
      if (edge.e->is_shortcut() || edge.e->use() == Use::kFerry) {
        continue;
      }

      if(
        paired_edge_ids.find(edge.i.value)          == paired_edge_ids.end() &&
        paired_edge_ids.find(opposing_edge.i.value) == paired_edge_ids.end()
      ) {
        paired_edges.push_back(std::make_pair(edge.i.value, opposing_edge.i.value));
        paired_edge_ids.insert(edge.i.value);
        paired_edge_ids.insert(opposing_edge.i.value);
      }

      // get the shape
      auto t = tile;
      auto ed = extend(reader, t, edge);

      if(ndumped++ > 0) {
        network_f  << ',' << '\n';
        edge_ids_f << ',' << '\n';
        osm_ids_f  << ',' << '\n';
        ll_f       << ',' << '\n';
      }

      // open the dict for the edge
      network_f  << '"' << std::get<0>(ed) << '"' << ":{";
      edge_ids_f        << std::get<0>(ed);

      // the latlons
      network_f << "\"lls\":[";
      ll_f      <<         '[';
      auto lls = std::get<1>(ed);
      auto nll = lls.size(), lli = 1ul;
      for(auto ll: lls) {
        network_f << '[' << ll.x() << ',' << ll.y() << ']';
        ll_f      << '[' << ll.x() << ',' << ll.y() << ']';
        if(lli++ < nll) {
          network_f << ',';
          ll_f      << ',';
        }
      }
      network_f << ']';
      ll_f      << ']';

      // the way id
      network_f << ",\"way_id\":" << tile->edgeinfo(edge.e).wayid();

      // the osmids
      network_f << ",\"osmids\":[";
      osm_ids_f <<             '[';
      auto osmids = std::get<2>(ed);
      auto nosmids = lls.size(), osmidi = 1ul;
      for(auto osmid: osmids) {
        network_f << osmid;
        osm_ids_f << osmid;
        if(osmidi++ < nosmids) {
          network_f << ',';
          osm_ids_f << ',';
        }
      }
      network_f << ']';
      osm_ids_f << ']';

      // close the dict for the edge
      network_f << '}';
    }
  }

  // close the list of edges
  edge_ids_f << "\n]";
  osm_ids_f  << "\n]";
  ll_f       << "\n]";
  network_f  << "\n}";

  // dump the opposing edge pairs
  ndumped = 0u;
  edge_pairs_f << '[';
  for(auto& ep: paired_edges) {
      if(ndumped++ != 0u) edge_pairs_f << ',';
      edge_pairs_f << '[' << ep.first << ',' << ep.second << ']';
  }
  edge_pairs_f << ']';

  LOG_INFO("Done");

  return EXIT_SUCCESS;
}
