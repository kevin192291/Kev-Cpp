---
label: Struct with Constructor/Destructor
description: A struct made by a _create() function and freed by a matching _destroy()
ask: Name = Player
order: 30
section: data
includes: <stdlib.h> <string.h>
---
typedef struct {{Name:pascal}}
{
    char* name;
    int score;
} {{Name:pascal}};

// Returns NULL when out of memory. Free it with {{Name:snake}}_destroy().
{{Name:pascal}}* {{Name:snake}}_create(const char* name)
{
    {{Name:pascal}}* self = calloc(1, sizeof *self);
    if (self == NULL)
    {
        return NULL;
    }
    size_t len = strlen(name) + 1;
    self->name = malloc(len);
    if (self->name == NULL)
    {
        free(self);
        return NULL;
    }
    memcpy(self->name, name, len);
    {{cursor}}
    return self;
}

// Accepts NULL, as free() does.
void {{Name:snake}}_destroy({{Name:pascal}}* self)
{
    if (self == NULL)
    {
        return;
    }
    free(self->name);
    free(self);
}
