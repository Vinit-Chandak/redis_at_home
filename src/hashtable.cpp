// hashmap.h - A simple hash map implementation with incremental rehashing
//
// This implementation uses separate chaining for collision resolution and
// supports incremental rehashing to avoid blocking operations.

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <stdlib.h>

// Maximum number of nodes to move during a single rehashing step
const size_t k_resizing_work = 128;

// Maximum average number of nodes per bucket before triggering rehash
const size_t k_max_load_factor = 8;

/**
 * @brief Base node structure for hash table entries
 *
 * Users of this hash table should extend this structure with their
 * own data fields (through inheritance or composition).
 */
struct HNode {
  HNode *next;       // Pointer to next node in the same bucket
  uint64_t hashcode; // Precomputed hash of the key
};

/**
 * @brief Single hash table structure
 *
 * Represents one hash table with a fixed size. Used internally
 * by HMap for the main table and rehashing table.
 */
struct HashTable {
  HNode **table = NULL; // Array of linked lists (buckets)
  uint64_t mask;        // Size mask (size-1) for efficient modulo
  uint64_t size;        // Number of keys in the table
};

/**
 * @brief Initialize a hash table with the specified size
 *
 * @param table Pointer to the hash table to initialize
 * @param size Size of the table (must be a power of 2)
 */
static void initHashTable(HashTable *table, uint64_t size) {
  // Ensure size is a power of 2
  assert(size > 0 && (size & (size - 1)) == 0);

  table->size = 0;
  table->mask = size - 1;
  table->table = (HNode **)calloc((size_t)size, sizeof(HNode *));
}

/**
 * @brief Insert a node into the hash table
 *
 * Nodes are inserted at the front of the bucket's linked list,
 * making insertion O(1) in the worst case.
 *
 * @param hashtable Pointer to the hash table
 * @param node Pointer to the node to insert
 */
static void h_insert(HashTable *hashtable, HNode *node) {
  uint64_t pos = node->hashcode & hashtable->mask;
  node->next = hashtable->table[pos];
  hashtable->table[pos] = node;
  hashtable->size++;
}

/**
 * @brief Look up a node in the hash table
 *
 * @param hashtable Pointer to the hash table
 * @param node Template node with the hashcode to look up
 * @param cmp Comparison function to determine if nodes match
 * @return Pointer to the pointer to the found node, or NULL if not found
 */
static HNode **h_lookup(HashTable *hashtable, HNode *node,
                        bool (*cmp)(HNode *, HNode *)) {
  if (hashtable->table == NULL) {
    return NULL;
  }

  int64_t bucket = node->hashcode & hashtable->mask;
  HNode **temp = &hashtable->table[bucket];

  while (*temp) {
    if (cmp(*temp, node)) {
      return temp;
    }
    temp = &((*temp)->next);
  }

  return NULL;
}

/**
 * @brief Detach a node from the hash table
 *
 * @param hashtable Pointer to the hash table
 * @param node Pointer to the pointer to the node to detach
 * @return Pointer to the detached node
 */
static HNode *h_detach(HashTable *hashtable, HNode **node) {
  HNode *temp = *node;
  *node = (*node)->next;
  hashtable->size--;
  return temp;
}

/**
 * @brief Hash map with incremental rehashing support
 *
 * The hash map contains two hash tables: h1 and h2.
 * h1 is the main table where new entries are inserted.
 * h2 is the old table during rehashing, from which entries
 * are gradually moved to h1.
 */
struct HMap {
  struct HashTable h1;      // Primary hash table
  struct HashTable h2;      // Secondary hash table (used during rehashing)
  int64_t resizing_pos = 0; // Current position for incremental rehashing
};

/**
 * @brief Perform incremental rehashing
 *
 * Moves a limited number of nodes from h2 to h1 during each call.
 * This prevents blocking operations when the hash table grows.
 *
 * @param hmap Pointer to the hash map
 */
static void hm_resizing(HMap *hmap) {
  // Check if the secondary table exists
  if (hmap->h2.table == nullptr) {
    return;
  }

  int64_t nodes_moved = 0;

  // Move up to k_resizing_work nodes from h2 to h1
  while (nodes_moved < k_resizing_work && hmap->h2.size > 0) {
    // Find the next non-empty bucket
    HNode **node = &hmap->h2.table[hmap->resizing_pos];
    if (*node == NULL) {
      hmap->resizing_pos++;
      continue;
    }

    // Move one node from h2 to h1
    h_insert(&hmap->h1, h_detach(&hmap->h2, node));
    nodes_moved++;
  }

  // Clean up h2 if it's empty
  if (hmap->h2.size == 0) {
    free(hmap->h2.table);
    hmap->h2 = HashTable{};
  }
}

/**
 * @brief Look up a node in the hash map
 *
 * Performs incremental rehashing and checks both hash tables.
 *
 * @param hmap Pointer to the hash map
 * @param node Template node with the hashcode to look up
 * @param cmp Comparison function to determine if nodes match
 * @return Pointer to the found node, or NULL if not found
 */
static HNode *hm_lookup(HMap *hmap, HNode *node,
                        bool (*cmp)(HNode *, HNode *)) {
  // Do some rehashing work
  hm_resizing(hmap);

  // Try to find the node in the primary table first
  HNode **from = h_lookup(&hmap->h1, node, cmp);

  // If not found in h1, try h2
  if (!from) {
    from = h_lookup(&hmap->h2, node, cmp);
  }

  return from ? *from : NULL;
}

/**
 * @brief Delete a node from the hash map
 *
 * Performs incremental rehashing and removes the node from both
 * hash tables if present.
 *
 * @param hmap Pointer to the hash map
 * @param node Template node with the hashcode to delete
 * @param cmp Comparison function to determine if nodes match
 * @return Pointer to the deleted node or NULL if not found
 */
static HNode *hm_delete(HMap *hmap, HNode *node,
                        bool (*cmp)(HNode *, HNode *)) {
  // Do some rehashing work
  hm_resizing(hmap);

  // Try to delete from h1
  if (HNode **from = h_lookup(&hmap->h1, node, cmp)) {
    return h_detach(&hmap->h1, from);
  }

  // Try to delete from h2
  if (HNode **from = h_lookup(&hmap->h2, node, cmp)) {
    return h_detach(&hmap->h2, from);
  }

  return NULL;
}

/**
 * @brief Trigger rehashing of the hash map
 *
 * Moves the current primary table to the secondary position and
 * creates a new, larger primary table.
 *
 * @param hmap Pointer to the hash map
 */
static void hm_trigger_rehashing(HMap *hmap) {
  // Move h1 to h2
  hmap->h2 = hmap->h1;

  // Create a new h1 with double the size
  initHashTable(&hmap->h1, ((hmap->h2.mask + 1) * 2));

  // Reset rehashing position
  hmap->resizing_pos = 0;
}

/**
 * @brief Insert a node into the hash map
 *
 * Initializes the hash map if necessary, performs the insertion,
 * and triggers rehashing if the load factor exceeds the threshold.
 *
 * @param hmap Pointer to the hash map
 * @param node Pointer to the node to insert
 */
static void hm_insert(HMap *hmap, HNode *node) {
  // Initialize the primary table if it doesn't exist
  if (!hmap->h1.table) {
    initHashTable(&hmap->h1, 4);
  }

  // Insert the node into the primary table
  h_insert(&hmap->h1, node);

  // Check if rehashing is needed
  if (!hmap->h2.table) {
    if (((hmap->h1.mask + 1) * k_max_load_factor) <= hmap->h1.size) {
      hm_trigger_rehashing(hmap);
    }
  }

  // Do some rehashing work
  hm_resizing(hmap);
}
