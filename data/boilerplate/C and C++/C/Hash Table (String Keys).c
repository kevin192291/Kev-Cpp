---
description: A hash table from strings to ints (set, get, remove) that grows as it fills
order: 33
section: data
includes: <stdbool.h> <stdint.h> <stdlib.h> <string.h>
---
// Keys are copied; change the type of value to store something else.
typedef struct HashEntry
{
    char* key;
    int value;
    struct HashEntry* next;
} HashEntry;

typedef struct HashTable
{
    HashEntry** buckets;
    size_t bucket_count;
    size_t count;
} HashTable;

// FNV-1a: short, and spreads similar strings well.
static uint64_t hash_string(const char* s)
{
    uint64_t h = 14695981039346656037u;
    for (; *s != '\0'; ++s)
    {
        h ^= (unsigned char)*s;
        h *= 1099511628211u;
    }
    return h;
}

// Returns NULL when out of memory.
HashTable* hash_table_create(void)
{
    HashTable* t = malloc(sizeof *t);
    if (t == NULL)
    {
        return NULL;
    }
    t->bucket_count = 16;
    t->count = 0;
    t->buckets = calloc(t->bucket_count, sizeof *t->buckets);
    if (t->buckets == NULL)
    {
        free(t);
        return NULL;
    }
    return t;
}

// The link that points at key's entry, or the empty link at the end of its bucket.
static HashEntry** hash_table_find(const HashTable* t, const char* key)
{
    HashEntry** link = &t->buckets[hash_string(key) % t->bucket_count];
    while (*link != NULL && strcmp((*link)->key, key) != 0)
    {
        link = &(*link)->next;
    }
    return link;
}

static bool hash_table_grow(HashTable* t)
{
    size_t bucket_count = t->bucket_count * 2;
    HashEntry** buckets = calloc(bucket_count, sizeof *buckets);
    if (buckets == NULL)
    {
        return false;
    }
    for (size_t i = 0; i < t->bucket_count; ++i)
    {
        HashEntry* e = t->buckets[i];
        while (e != NULL)
        {
            HashEntry* next = e->next;
            size_t b = hash_string(e->key) % bucket_count;
            e->next = buckets[b];
            buckets[b] = e;
            e = next;
        }
    }
    free(t->buckets);
    t->buckets = buckets;
    t->bucket_count = bucket_count;
    return true;
}

// Adds key or replaces its value. Returns false when out of memory.
bool hash_table_set(HashTable* t, const char* key, int value)
{
    HashEntry** link = hash_table_find(t, key);
    if (*link != NULL)
    {
        (*link)->value = value;
        return true;
    }
    // Growing is only an optimisation: if it fails the table still works, just slower.
    if (t->count >= t->bucket_count / 4 * 3 && hash_table_grow(t))
    {
        link = hash_table_find(t, key);
    }
    size_t len = strlen(key) + 1;
    HashEntry* e = malloc(sizeof *e);
    char* copy = malloc(len);
    if (e == NULL || copy == NULL)
    {
        free(e);
        free(copy);
        return false;
    }
    memcpy(copy, key, len);
    e->key = copy;
    e->value = value;
    e->next = NULL;
    *link = e;
    t->count++;
    return true;
}

// Copies key's value into *value; false if key isn't there.
bool hash_table_get(const HashTable* t, const char* key, int* value)
{
    const HashEntry* e = *hash_table_find(t, key);
    if (e == NULL)
    {
        return false;
    }
    *value = e->value;
    return true;
}

// Returns false if key wasn't there.
bool hash_table_remove(HashTable* t, const char* key)
{
    HashEntry** link = hash_table_find(t, key);
    HashEntry* e = *link;
    if (e == NULL)
    {
        return false;
    }
    *link = e->next;
    free(e->key);
    free(e);
    t->count--;
    return true;
}

// Accepts NULL.
void hash_table_destroy(HashTable* t)
{
    if (t == NULL)
    {
        return;
    }
    for (size_t i = 0; i < t->bucket_count; ++i)
    {
        HashEntry* e = t->buckets[i];
        while (e != NULL)
        {
            HashEntry* next = e->next;
            free(e->key);
            free(e);
            e = next;
        }
    }
    free(t->buckets);
    free(t);
}
