#ifndef LILY_VALUE_STRUCTS_H
# define LILY_VALUE_STRUCTS_H

# include <stdint.h>
# include <stdio.h>

struct lily_vm_state_;
struct lily_value_;

/* Lily's foreign functions look like this. */
typedef void (*lily_foreign_func)(struct lily_vm_state_ *);

/* lily_raw_value is a union of all possible values, plus a bit more. This is
   not common, because lily_value (which has flags and a type) is typically
   used for parameters and such instead. However, this does have some uses. */
typedef union lily_raw_value_ {
    int64_t integer;
    double doubleval;
    struct lily_string_val_ *string;
    struct lily_dynamic_val_ *dynamic;
    struct lily_list_val_ *list;
    /* generic is a subset of any type that is refcounted. */
    struct lily_generic_val_ *generic;
    /* gc_generic is a subset of any type that is refcounted AND has a gc
       entry. */
    struct lily_generic_gc_val_ *gc_generic;
    struct lily_function_val_ *function;
    struct lily_hash_val_ *hash;
    struct lily_file_val_ *file;
    struct lily_instance_val_ *instance;
    struct lily_foreign_val_ *foreign;
} lily_raw_value;

/* A literal represents some value that needs to be stored until the vm is ready
   to receive it. These come in the following flavors:
   * Foreign values, which will be consumed by the vm to initialize globals.
     These don't need to store any additional information.
   * The common kind of literals: Integers, Strings, and so on. These each use
     the 'next_index' to store where the next of their group is, so that symtab
     can search through them faster. The last one will have 'next_index' set to
     0.

   It is both intentional and important that these are the same size as a real
   value. This allows them to be manipulated by the vm using value-handling
   functions, as if they were a real value...even if they aren't. */
typedef struct lily_literal_ {
    union {
        uint16_t class_id;
        uint32_t flags;
    };
    union {
        uint16_t pad;
        uint16_t next_index;
    };
    uint16_t reg_spot;
    lily_raw_value value;
} lily_literal;

/* This holds a value that has been deemed interesting to the gc. This has the
   same layout as a lily_value_ on purpose. */
typedef struct lily_gc_entry_ {
    /* The flags from the value. Used to determine what's inside the value. */
    uint32_t flags;
    /* Each gc pass has a different number that always goes up. If an entry's
       last_pass is not the current number, then the contained value is
       destroyed. */
    int32_t last_pass;
    /* If this is destroyed outside of the gc, then value.generic should be set
       to NULL to keep the gc from looking at an invalid data. */
    lily_raw_value value;
    struct lily_gc_entry_ *next;
} lily_gc_entry;

/* A proper Lily value. The flags field holds markers for gc and other info in
   the upper 16 bits. The lower 16 bits are reserved for the class id. The
   class_id field should only be used for reading, never writing. */
typedef struct lily_value_ {
    union {
        uint32_t flags;
        uint16_t class_id;
    };
    /* This is only used by closure cells. When a closure cell has a
       cell_refcount of zero, it's deref'd and free'd. */
    uint32_t cell_refcount;
    lily_raw_value value;
} lily_value;

/* This is a string. It's pretty simple. These are refcounted. */
typedef struct lily_string_val_ {
    uint32_t refcount;
    uint32_t size;
    char *string;
} lily_string_val;

/* Internally, ByteString values are represented by strings. This exists apart
   from lily_string_val so that API can't assume they're the same (in case they
   diverge). */
typedef struct lily_bytestring_val_ {
    uint32_t refcount;
    uint32_t size;
    char *string;
} lily_bytestring_val;

/* Instances of the Dynamic class act as a wrapper around some singular value.
   Their padding exists so that their gc entry aligns with that of instances. */
typedef struct lily_dynamic_val_ {
    uint32_t refcount;
    uint32_t pad1;
    uint64_t pad2;
    struct lily_value_ *inner_value;
    struct lily_gc_entry_ *gc_entry;
} lily_dynamic_val;

/* Internally, (non-empty) variants have the same layout as instances. This is
   provided so that API can't make the same assumption, in case that changes. */
typedef struct lily_variant_val_ {
    uint32_t refcount;
    uint16_t pad1;
    uint16_t pad2;
    uint32_t num_values;
    uint32_t pad3;
    struct lily_value_ **values;
    struct lily_gc_entry_ *gc_entry;
} lily_variant_val;

typedef struct lily_list_val_ {
    uint32_t refcount;
    uint32_t extra_space;
    uint32_t num_values;
    uint32_t pad;
    struct lily_value_ **elems;
} lily_list_val;

typedef struct lily_hash_entry_ {
    unsigned int hash;
    char *raw_key;
    lily_value *boxed_key;
    lily_value *record;
    struct lily_hash_entry_ *next;
} lily_hash_entry;

typedef struct lily_hash_val_ {
    uint32_t refcount;
    uint32_t iter_count;
    int (*compare_fn)();
    int (*hash_fn)();
    int num_bins;
    int num_entries;
    lily_hash_entry **bins;
} lily_hash_val;

/* Either an instance or an enum. This structure has extra padding so that it
   lines up with lists (and thus with tuples). This may be changed in the
   future, but it won't mater to the API. */
typedef struct lily_instance_val_ {
    uint32_t refcount;
    uint16_t pad1;
    /* How many constructors need to visit this value before it's done? */
    uint16_t ctor_need;
    uint32_t num_values;
    uint32_t pad2;
    struct lily_value_ **values;
    struct lily_gc_entry_ *gc_entry;
} lily_instance_val;

typedef struct lily_file_val_ {
    uint32_t refcount;
    uint8_t read_ok;
    uint8_t write_ok;
    uint8_t is_builtin;
    uint8_t pad1;
    uint32_t pad2;
    FILE *inner_file;
} lily_file_val;

/* Finally, functions. Functions come in two flavors: Native and foreign.
   * Native:  This function is declared and defined by a user. It has a code
              section (which the vm will execute), and has to initialize
              registers that it will use.
   * Foreign: This function is automatically created by the interpreter. The
              implementation is defined outside of Lily code.

   These two are mutually exclusive (a function must never have a foreign_func
   set AND code). The interpreter makes no difference between either function
   (it's the vm's job to do the right call), so they're both passable as
   arguments to a function themselves. */
typedef struct lily_function_val_ {
    uint32_t refcount;
    uint32_t line_num;

    uint16_t pad;

    uint16_t code_len;

    uint16_t num_upvalues;

    /* This is how many registers that this function uses. */
    uint16_t reg_count;

    /* The module that this function was created within. */
    struct lily_module_entry_ *module;

    struct lily_gc_entry_ *gc_entry;

    /* The name of the class that this function belongs to OR "". */
    const char *class_name;

    /* The name of this function, for use by debug and stack trace. */
    const char *trace_name;

    /* Foreign functions only. To determine if a function is foreign, simply
       check 'foreign_func == NULL'. */
    lily_foreign_func foreign_func;

    char *docstring;

    /* Here's where the function's code is stored. */
    uint16_t *code;

    union {
        struct lily_value_ **upvalues;
        /* A function's cid table holds a mapping that's used to obtain class
           ids for dynaloaded classes. */
        uint16_t *cid_table;
    };
} lily_function_val;

/* Internally, Tuple has the same layout as List. This is provided so that API
   can't make the same assumption, in case that changes. */
typedef struct lily_tuple_val_ {
    uint32_t refcount;
    uint32_t extra_space;
    uint32_t num_values;
    uint32_t pad;
    struct lily_value_ **elems;
} lily_tuple_val;

/* Every value that is refcounted is a superset of this. */
typedef struct lily_generic_val_ {
    uint32_t refcount;
} lily_generic_val;

/* Foreign values must start with at least this layout. Note that this carries
   the implicit assumption that no foreign value will carry Lily values inside.
   Should that change, this too many need to be changed. */
typedef struct lily_foreign_val_ {
    uint32_t refcount;
    uint32_t pad;
    void (*destroy_func)(lily_generic_val *);
} lily_foreign_val;

/* Every value that has a gc entry is a superset of this. */
typedef struct lily_generic_gc_val_ {
    uint32_t refcount;
    uint32_t pad;
    uint64_t pad2;
    void *pad3;
    struct lily_gc_entry_ *gc_entry;
} lily_generic_gc_val;

#endif
