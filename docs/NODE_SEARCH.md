# Finding a node by describing it

The library's filter used to be a substring match on the type name, which
finds a node only if you already know what it is called. Two things need to
find a node and neither knows that: a person who knows what they *want*
("rocks", "wear the mountains down"), and the assistant turning a sentence
into a graph.

Type `rocks` into the library and you get **Stone field**, **Stones in the
heightmap**, **Scree and talus**, **Fine surface grit**, **Gravel** — none of
which contains the word "rock" anywhere.

## What it is

A **vector space over the node catalogue**. Every node becomes a sparse
TF-IDF vector over the words in its name, display name, category, description,
parameter labels, tooltips and port names. A query becomes a vector the same
way. The score is the cosine between them.

Three things make the results usable rather than merely computable:

- **Fields are weighted.** A word in the display name (×6) says far more than
  the same word in a tooltip (×1). Without this a node with forty parameters
  outranks the node actually *named* after what you searched for, purely for
  mentioning it more often.
- **The query expands through a concept table** before scoring, so "rocks"
  reaches nodes that only ever say "stone", "scree" or "talus". An expanded
  word counts 0.45 against a typed word's 1.0, so an exact hit always outranks
  an associated one — searching "stones" puts the stone nodes above the gravel
  ones rather than merely among them.
- **IDF is computed over the corpus**, so "terrain" — which nearly every node
  mentions — stops being evidence, while "caldera" is decisive.

## What it is not

**It is not a neural embedding.** It generalises exactly as far as the concept
table in `engine/node_search_terms.cpp` does, and no further. That is a
deliberate trade: it is deterministic, it needs no model, no network and no
Python, it builds in milliseconds from the registry itself, and it can be
tested. `gpx::search::find_nodes` is the seam an embedding backend would slot
behind if that ever changes.

## The concept table

Rows of words that mean nearly the same thing *for the purpose of finding a
node*. Membership is symmetric — any word in a row expands to the whole row —
and rows may overlap, because "water" belongs to both hydrology and erosion.

Widening the vocabulary is a data change in one file that anyone can make and
review without touching the scoring.

**One row is worth calling out for what it does not contain.** "cloud" is not
in the points row. A *point cloud* is a cloud only to a programmer; to
everyone else the word means the thing in the sky, and having it in both rows
made "clouds at sunset" return the scatter nodes. "point cloud" still finds
them, through "point".

## Where it is used

- **The library panel.** Typing turns the category list into a ranked flat
  list, with the category shown beside each hit and an "also searched:" line
  underneath, so a surprising result is explainable rather than mysterious.
- **The API and MCP**, as `{"op":"find_nodes","query":"...","limit":12}`,
  which is how the assistant finds the node for a step of a workflow without
  being handed the whole catalogue.

## Where it is verified

`tests/cpp/test_node_search.cpp`, 42 checks: that a word in no node's text
still finds the right nodes, that an exact match outranks an associated one,
that a phrase works as well as a keyword, that the display name is searchable
(the node labelled "River incision" is found by that, though its type says
`StreamPower`), that nothing matches when nothing should, and that the ranking
is identical after an index rebuild — a search that reorders between runs
cannot be trusted or tested.

Two defects came out of running it against the live application rather than
only the test:

- the expansion shown to the user was made of **stems**, not words: "also
  searched: ston, talu, lak". Scoring works on stems; the interface must not.
- "clouds at sunset" returned the scatter nodes, via the point-cloud
  collision above.
