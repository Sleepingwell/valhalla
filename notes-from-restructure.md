- In *graphtileheader.h*, I have put the offest to to the osmids after the
  declaration of *tile_size_*. This does not feel right (is this the right place).

- Was not sure if I should be keeping a ptr to block of memory in the graphtile,
  or, as you said in your reply, a vector. The latter appears to waste space after
  the tile is loaded, so I did the former.

- Was not sure if I wrote the osmids in the correct place in the file.

- Was not sure what to do if an OSM ids was requested, but there were no OSM ids.
  In this case I returned `std::numeric_limits<uint64_t>::max()`.

- in GraphTile I have a method `has_osmids`. proobaly not required given if the
  approach taken in the previous point is sensible. Might be better to put this
  in GraphTileHeader if it is required.

- Was not sure about naming/case of methods on GraphTile.

- I've used assertions in a few spots. Would you like these changed to standard
  checks?

- Please look at the new TODOs.

- We get either:
    - ids for edges and nodes, or
    - ids for neither edges or nodes.

- `GraphTileBuilder::SetHasOSMIds(bool)` is required from `::FilterTiles(...)`,
  as I was not sure if it is ever possible that there would be no ids set by the
  time `GraphTileBuilder::AddEdgeInfo(...)` was called (around line 150).

- The logic of `GraphTileBuilder::has_osmids()` is a bit yucky. I keep a bool
  `GraphTileBuilder::has_osmids_` which can get set by
  `GraphTileBuilder::has_osmids(bool)`.  This is also reset from the
  constructor `GraphTileBuilder(...)` from the `GraphTile::header_` via a call to
  `GrapTileHeader::has_osmids()`. Note that `GraphTileBuilder::has_osmids(bool)`
  masks `GraphTile::has_osmids()`

  `GraphTile::has_osmids()` on the other hand calls `GraphTileHeader::has_osmids()`,
  which checks if any OSM ids have been added to the tile.

  This could cause problems if a `GraphTileBuilder` is sliced to a `GraphTile`
  when it has not been loaded from an existing `GraphTile` on disk. In this case
  a user might check the state using `GraphTile::has_osmids()`, find it is
  `false` and conclude that the tile has no OSM ids when in fact it does.

  Another approach would be to keep a bool in `GraphTileHeader::has_osmids_`,
  and set this 'directly' from `GraphTileBuilder::has_osmids(bool)`. In this
  case, if a `GraphTileBuilder` is sliced to a `GraphTile` then a user might
  check the state using `GraphTile::has_osmids()`, find it is `false` and
  conclude that the tile has OSM ids, but the OSM ids data is not in a consistent
  state (I've not checked at what stage the state would become consistent), which
  could lead to unexpected behaviour or seg faults. This seems like a potentially
  worse set of problems than finding there are no OSM ids when in fact there are,
  and hence my choice of solutions.

  I think even with the chosen implementation it is possible that
  inconsistencies could arise if a `GraphTileBuilder` instance is loaded from a
  `GraphTile` on disk, modified and then accessed as a `GraphTile`, but I've not
  checked this.

  I think the bigger problem here is that `GraphTileBuilder` extends
  `GraphTile`, though I humbly admit that I don't know enough about Valhalla
  understand why this is the case or if there are practical alternatives.

- I think the asserts in `GraphTileBuilder::StoreTileData(...)` confirm that
  transit does not break things (as was suggested in original advice).



## TODOs

- Only write OSM ids to ouptuts if requested (regardless of if they are in the tile).

- Deal with indentation change in call to `AddEdgeInfo` in graphtilebuilder.cc.

- Check if appending to OSM info around line 156 of *graphfilter.cc* is safe.



## Other questions/comments

- Could not help but modify implementations of `GraphTile::node(const GraphId& node);`.
  and `GraphTile::directededge(const GraphId& edge)`. Sorry.

- Why does `mjolnir::GraphTileBuild` extend `baldr::GraphTile`?
