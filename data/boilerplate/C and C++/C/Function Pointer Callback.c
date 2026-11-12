---
description: A callback type with a user-data pointer, a function that calls it, and a callback
order: 40
section: functions
---
// The void* carries the caller's own state into the callback, so no globals are needed.
typedef void (*VisitFn)(int value, void* user_data);

void for_each(const int* items, size_t count, VisitFn visit, void* user_data)
{
    for (size_t i = 0; i < count; ++i)
    {
        visit(items[i], user_data);
    }
}

// A callback that adds each value to the total user_data points to:
//     int total = 0;
//     for_each(numbers, count, add_to_total, &total);
static void add_to_total(int value, void* user_data)
{
    int* total = user_data;
    *total += value;
}
