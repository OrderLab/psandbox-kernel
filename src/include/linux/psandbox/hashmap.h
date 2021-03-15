//
// The Psandbox project
//
// Created by yigonghu on 2/25/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#ifndef PSANDBOX_USERLIB_HASHMAP_H
#define PSANDBOX_USERLIB_HASHMAP_H

#ifndef SHEREDOM_HASHMAP_H_INCLUDED
#define SHEREDOM_HASHMAP_H_INCLUDED

#if defined(_MSC_VER)
// Workaround a bug in the MSVC runtime where it uses __cplusplus when not
// defined.
#pragma warning(push, 0)
#pragma warning(disable : 4668)
#endif

#if (defined(_MSC_VER) && defined(__AVX__)) ||                                 \
	(!defined(_MSC_VER) && defined(__SSE4_2__))
#define HASHMAP_SSE42
#endif

#if defined(HASHMAP_SSE42)
#include <nmmintrin.h>
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_MSC_VER)
#pragma warning(push)
/* Stop MSVC complaining about unreferenced functions */
#pragma warning(disable : 4505)
/* Stop MSVC complaining about not inlining functions */
#pragma warning(disable : 4710)
/* Stop MSVC complaining about inlining functions! */
#pragma warning(disable : 4711)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#if defined(_MSC_VER)
#define HASHMAP_USED
#elif defined(__GNUC__)
#define HASHMAP_USED __attribute__((used))
#else
#define HASHMAP_USED
#endif

/* We need to keep keys and values. */
struct hashmap_element_s {
	unsigned long key;
	int in_use;
	void *data;
};

/* A hashmap has some maximum size and current size, as well as the data to
 * hold. */
typedef struct hashmap_s {
	unsigned table_size;
	unsigned size;
	struct hashmap_element_s *data;
} HashMap;

#define HASHMAP_MAX_CHAIN_LENGTH (8)

#if defined(__cplusplus)
extern "C" {
#endif

/// @brief Create a hashmap.
/// @param initial_size The initial size of the hashmap. Must be a power of two.
/// @param out_hashmap The storage for the created hashmap.
/// @return On success 0 is returned.
///
/// Note that the initial size of the hashmap must be a power of two, and
/// creation of the hashmap will fail if this is not the case.
static int hashmap_create(const unsigned initial_size,
			  struct hashmap_s *const out_hashmap) HASHMAP_USED;

/// @brief Put an element into the hashmap.
/// @param hashmap The hashmap to insert into.
/// @param key The string key to use.
/// @param len The length of the string key.
/// @param value The value to insert.
/// @return On success 0 is returned.
///
/// The key string slice is not copied when creating the hashmap entry, and thus
/// must remain a valid pointer until the hashmap entry is removed or the
/// hashmap is destroyed.
static int hashmap_put(struct hashmap_s *const hashmap, unsigned long key,
		       void *const value) HASHMAP_USED;

/// @brief Get an element from the hashmap.
/// @param hashmap The hashmap to get from.
/// @param key The string key to use.
/// @param len The length of the string key.
/// @return The previously set element, or NULL if none exists.
static void *hashmap_get(const struct hashmap_s *const hashmap,
			 unsigned long key) HASHMAP_USED;

/// @brief Remove an element from the hashmap.
/// @param hashmap The hashmap to remove from.
/// @param key The string key to use.
/// @param len The length of the string key.
/// @return On success 0 is returned.
static int hashmap_remove(struct hashmap_s *const hashmap,
			  unsigned long key) HASHMAP_USED;

/// @brief Remove an element from the hashmap.
/// @param hashmap The hashmap to remove from.
/// @param key The string key to use.
/// @param len The length of the string key.
/// @return On success the original stored key pointer is returned, on failure
/// NULL is returned.
static const char *
hashmap_remove_and_return_key(struct hashmap_s *const hashmap,
			      unsigned long key) HASHMAP_USED;

/// @brief Iterate over all the elements in a hashmap.
/// @param hashmap The hashmap to iterate over.
/// @param f The function pointer to call on each element.
/// @param context The context to pass as the first argument to f.
/// @return If the entire hashmap was iterated then 0 is returned. Otherwise if
/// the callback function f returned non-zero then non-zero is returned.
static int hashmap_iterate(const struct hashmap_s *const hashmap,
			   int (*f)(void *const context, void *const value),
			   void *const context) HASHMAP_USED;

/// @brief Iterate over all the elements in a hashmap.
/// @param hashmap The hashmap to iterate over.
/// @param f The function pointer to call on each element.
/// @param context The context to pass as the first argument to f.
/// @return If the entire hashmap was iterated then 0 is returned.
/// Otherwise if the callback function f returned positive then the positive
/// value is returned.  If the callback function returns -1, the current item
/// is removed and iteration continues.
static int hashmap_iterate_pairs(struct hashmap_s *const hashmap,
				 int (*f)(void *const,
					  struct hashmap_element_s *const),
				 void *const context) HASHMAP_USED;

/// @brief Get the size of the hashmap.
/// @param hashmap The hashmap to get the size of.
/// @return The size of the hashmap.
static unsigned
hashmap_num_entries(const struct hashmap_s *const hashmap) HASHMAP_USED;

/// @brief Destroy the hashmap.
/// @param hashmap The hashmap to destroy.
static void hashmap_destroy(struct hashmap_s *const hashmap) HASHMAP_USED;

static unsigned hashmap_hash_helper_int_helper(const struct hashmap_s *const m,
					       unsigned long key) HASHMAP_USED;
static int hashmap_match_helper(const struct hashmap_element_s *const element,
				unsigned long key) HASHMAP_USED;
static int hashmap_hash_helper(const struct hashmap_s *const m, unsigned long key,
			       unsigned *const out_index) HASHMAP_USED;
static int
hashmap_rehash_iterator(void *const new_hash,
			struct hashmap_element_s *const e) HASHMAP_USED;
static int hashmap_rehash_helper(struct hashmap_s *const m) HASHMAP_USED;

#if defined(__cplusplus)
}
#endif

#if defined(__cplusplus)
#define HASHMAP_CAST(type, x) static_cast<type>(x)
#define HASHMAP_PTR_CAST(type, x) reinterpret_cast<type>(x)
#define HASHMAP_NULL NULL
#else
#define HASHMAP_CAST(type, x) ((type)x)
#define HASHMAP_PTR_CAST(type, x) ((type)x)
#define HASHMAP_NULL 0
#endif

int hashmap_create(const unsigned initial_size,
		   struct hashmap_s *const out_hashmap)
{
	out_hashmap->table_size = initial_size;
	out_hashmap->size = 0;

	if (0 == initial_size || 0 != (initial_size & (initial_size - 1))) {
		return 1;
	}

	out_hashmap->data = HASHMAP_CAST(
		struct hashmap_element_s *,
		kmalloc(initial_size, sizeof(struct hashmap_element_s)));
	if (!out_hashmap->data) {
		return 1;
	}

	return 0;
}

int hashmap_put(struct hashmap_s *const m, unsigned long key, void *const value)
{
	unsigned int index;

	/* Find a place to put our value. */
	while (!hashmap_hash_helper(m, key, &index)) {
		if (hashmap_rehash_helper(m)) {
			return 1;
		}
	}

	/* Set the data. */
	m->data[index].data = value;
	m->data[index].key = key;

	/* If the hashmap element was not already in use, set that it is being used
   * and bump our size. */
	if (0 == m->data[index].in_use) {
		m->data[index].in_use = 1;
		m->size++;
	}

	return 0;
}

void *hashmap_get(const struct hashmap_s *const m, unsigned long key)
{
	unsigned int curr;
	unsigned int i;

	/* Find data location */
	curr = hashmap_hash_helper_int_helper(m, key);

	/* Linear probing, if necessary */
	for (i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++) {
		if (m->data[curr].in_use) {
			if (hashmap_match_helper(&m->data[curr], key)) {
				return m->data[curr].data;
			}
		}

		curr = (curr + 1) % m->table_size;
	}

	/* Not found */
	return HASHMAP_NULL;
}

int hashmap_remove(struct hashmap_s *const m, unsigned long key)
{
	unsigned int i;
	unsigned int curr;

	/* Find key */
	curr = hashmap_hash_helper_int_helper(m, key);

	/* Linear probing, if necessary */
	for (i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++) {
		if (m->data[curr].in_use) {
			if (hashmap_match_helper(&m->data[curr], key)) {
				/* Blank out the fields including in_use */
				memset(&m->data[curr], 0,
				       sizeof(struct hashmap_element_s));

				/* Reduce the size */
				m->size--;

				return 0;
			}
		}

		curr = (curr + 1) % m->table_size;
	}

	return 1;
}

const char *hashmap_remove_and_return_key(struct hashmap_s *const m,
					  unsigned long key)
{
	unsigned int i;
	unsigned int curr;

	/* Find key */
	curr = hashmap_hash_helper_int_helper(m, key);

	/* Linear probing, if necessary */
	for (i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++) {
		if (m->data[curr].in_use) {
			if (hashmap_match_helper(&m->data[curr], key)) {
				const char *const stored_key =
					m->data[curr].key;

				/* Blank out the fields */
				m->data[curr].in_use = 0;
				m->data[curr].data = HASHMAP_NULL;
				m->data[curr].key = HASHMAP_NULL;

				/* Reduce the size */
				m->size--;

				return stored_key;
			}
		}
		curr = (curr + 1) % m->table_size;
	}

	return HASHMAP_NULL;
}

int hashmap_iterate(const struct hashmap_s *const m,
		    int (*f)(void *const, void *const), void *const context)
{
	unsigned int i;

	/* Linear probing */
	for (i = 0; i < m->table_size; i++) {
		if (m->data[i].in_use) {
			if (!f(context, m->data[i].data)) {
				return 1;
			}
		}
	}
	return 0;
}

int hashmap_iterate_pairs(struct hashmap_s *const hashmap,
			  int (*f)(void *const,
				   struct hashmap_element_s *const),
			  void *const context)
{
	unsigned int i;
	struct hashmap_element_s *p;
	int r;

	/* Linear probing */
	for (i = 0; i < hashmap->table_size; i++) {
		p = &hashmap->data[i];
		if (p->in_use) {
			r = f(context, p);
			switch (r) {
			case -1: /* remove item */
				memset(p, 0, sizeof(struct hashmap_element_s));
				hashmap->size--;
				break;
			case 0: /* continue iterating */
				break;
			default: /* early exit */
				return 1;
			}
		}
	}
	return 0;
}

void hashmap_destroy(struct hashmap_s *const m)
{
	kfree(m->data);
	memset(m, 0, sizeof(struct hashmap_s));
}

unsigned hashmap_num_entries(const struct hashmap_s *const m)
{
	return m->size;
}

unsigned hashmap_hash_helper_int_helper(const struct hashmap_s *const m,
					unsigned long key)
{
	/* Robert Jenkins' 32 bit Mix Function */
	key += (key << 12);
	key ^= (key >> 22);
	key += (key << 4);
	key ^= (key >> 9);
	key += (key << 10);
	key ^= (key >> 2);
	key += (key << 7);
	key ^= (key >> 12);

	/* Knuth's Multiplicative Method */
	key = (key >> 3) * 2654435761;

	return key % m->table_size;
}

int hashmap_match_helper(const struct hashmap_element_s *const element,
			 unsigned long key)
{
	return (element->key == key);
}

int hashmap_hash_helper(const struct hashmap_s *const m, unsigned long key,
			unsigned *const out_index)
{
	unsigned int start, curr;
	unsigned int i;
	int total_in_use;

	/* If full, return immediately */
	if (m->size >= m->table_size) {
		return 0;
	}

	/* Find the best index */
	curr = start = hashmap_hash_helper_int_helper(m, key);

	/* First linear probe to check if we've already insert the element */
	total_in_use = 0;

	for (i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++) {
		const int in_use = m->data[curr].in_use;

		total_in_use += in_use;

		if (in_use && hashmap_match_helper(&m->data[curr], key)) {
			*out_index = curr;
			return 1;
		}

		curr = (curr + 1) % m->table_size;
	}

	curr = start;

	/* Second linear probe to actually insert our element (only if there was at
   * least one empty entry) */
	if (HASHMAP_MAX_CHAIN_LENGTH > total_in_use) {
		for (i = 0; i < HASHMAP_MAX_CHAIN_LENGTH; i++) {
			if (!m->data[curr].in_use) {
				*out_index = curr;
				return 1;
			}

			curr = (curr + 1) % m->table_size;
		}
	}

	return 0;
}

int hashmap_rehash_iterator(void *const new_hash,
			    struct hashmap_element_s *const e)
{
	int temp = hashmap_put(HASHMAP_PTR_CAST(struct hashmap_s *, new_hash),
			       e->key, e->data);
	if (0 < temp) {
		return 1;
	}
	/* clear old value to avoid stale pointers */
	return -1;
}
/*
 * Doubles the size of the hashmap, and rehashes all the elements
 */
int hashmap_rehash_helper(struct hashmap_s *const m)
{
	/* If this multiplication overflows hashmap_create will fail. */
	unsigned new_size = 2 * m->table_size;

	struct hashmap_s new_hash;

	int flag = hashmap_create(new_size, &new_hash);

	if (0 != flag) {
		return flag;
	}

	/* copy the old elements to the new table */
	flag = hashmap_iterate_pairs(m, hashmap_rehash_iterator,
				     HASHMAP_PTR_CAST(void *, &new_hash));

	if (0 != flag) {
		return flag;
	}

	hashmap_destroy(m);
	/* put new hash into old hash structure by copying */
	memcpy(m, &new_hash, sizeof(struct hashmap_s));

	return 0;
}

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif

#endif //PSANDBOX_USERLIB_HASHMAP_H
