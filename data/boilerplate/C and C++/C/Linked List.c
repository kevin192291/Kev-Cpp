---
description: A singly linked list of ints: push to the front, remove a value, free the list
order: 32
section: data
includes: <stdbool.h> <stdlib.h>
---
// An empty list is a NULL head: ListNode* head = NULL; Go through it with
//     for (ListNode* n = head; n != NULL; n = n->next) { ... }
typedef struct ListNode
{
    int value;
    struct ListNode* next;
} ListNode;

// Returns false when out of memory.
bool list_push_front(ListNode** head, int value)
{
    ListNode* node = malloc(sizeof *node);
    if (node == NULL)
    {
        return false;
    }
    node->value = value;
    node->next = *head;
    *head = node;
    return true;
}

// Removes the first node holding value; false if there is none. Walking the links (not the nodes)
// means the head needs no special case.
bool list_remove(ListNode** head, int value)
{
    for (ListNode** link = head; *link != NULL; link = &(*link)->next)
    {
        if ((*link)->value == value)
        {
            ListNode* node = *link;
            *link = node->next;
            free(node);
            return true;
        }
    }
    return false;
}

void list_free(ListNode** head)
{
    ListNode* node = *head;
    while (node != NULL)
    {
        ListNode* next = node->next;
        free(node);
        node = next;
    }
    *head = NULL;
}
