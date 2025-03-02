#ifndef HASHTABLE_H
#define HASHTABLE_H

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

void initHashTable(HashTable *table, uint64_t size);
void h_insert(HashTable *hashtable, HNode *node);
HNode **h_lookup(HashTable *hashtable, HNode *node,
                 bool (*cmp)(HNode *, HNode *));
HNode *h_detach(HashTable *hashtable, HNode **node);
void hm_resizing(HMap *hmap);
HNode *hm_lookup(HMap *hmap, HNode *node, bool (*cmp)(HNode *, HNode *));
HNode *hm_delete(HMap *hmap, HNode *node, bool (*cmp)(HNode *, HNode *));
void hm_trigger_rehashing(HMap *hmap);
void hm_insert(HMap *hmap, HNode *node);

#endif
