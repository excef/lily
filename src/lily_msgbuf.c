#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "lily_core_types.h"
#include "lily_value_flags.h"
#include "lily_vm.h"

#include "lily_api_alloc.h"
#include "lily_api_msgbuf.h"

extern lily_type *lily_unit_type;

/* The internals of msgbuf are very simple. Unlike most declarations, this one
   is intentionally not within a .h file. The reasoning behind that, is that
   this prevents other parts of the interpreter from grabbing the message field
   directly. */
typedef struct lily_msgbuf_ {
    /* The message being stored. */
    char *message;
    /* The size that the message currently takes. */
    uint32_t pos;
    /* The buffer space allocated for the message. */
    uint32_t size;
} lily_msgbuf;

lily_msgbuf *lily_new_msgbuf(uint32_t initial)
{
    lily_msgbuf *msgbuf = lily_malloc(sizeof(lily_msgbuf));

    msgbuf->message = lily_malloc(initial * sizeof(char));
    msgbuf->message[0] = '\0';
    msgbuf->pos = 0;
    msgbuf->size = initial;

    return msgbuf;
}

static void resize_msgbuf(lily_msgbuf *msgbuf, int new_size)
{
    while (msgbuf->size < new_size)
        msgbuf->size *= 2;

    msgbuf->message = lily_realloc(msgbuf->message, msgbuf->size);
}

static void add_escaped_char(lily_msgbuf *msgbuf, char ch)
{
    char buffer[16];
    sprintf(buffer, "%03d", (unsigned char)ch);

    lily_mb_add(msgbuf, buffer);
}

static char get_escape(char ch)
{
    if (ch == '\n')
        ch = 'n';
    else if (ch == '\r')
        ch = 'r';
    else if (ch == '\t')
        ch = 't';
    else if (ch == '\'')
        ch = '\'';
    else if (ch == '"')
        ch = '"';
    else if (ch == '\\')
        ch = '\\';
    else if (ch == '\b')
        ch = 'b';
    else if (ch == '\a')
        ch = 'a';
    else
        ch = 0;

    return ch;
}

/* Add a safe, escaped version of a given string to the msgbuf. A size is given
   as well so that bytestrings may be sent. */
static void add_escaped_raw(lily_msgbuf *msgbuf, int is_bytestring,
        const char *str, int len)
{
    char escape_char = 0;
    int i, start;

    for (i = 0, start = 0;i < len;i++) {
        char ch = str[i];
        int need_escape = 1;

        if (isprint(ch) ||
            ((unsigned char)ch > 127 && is_bytestring == 0)) {
            need_escape = 0;
            escape_char = 0;
        }
        else
            escape_char = get_escape(ch);

        if (need_escape) {
            if (i != start)
                lily_mb_add_slice(msgbuf, str, start, i);

            lily_mb_add_char(msgbuf, '\\');
            if (escape_char)
                lily_mb_add_char(msgbuf, escape_char);
            else
                add_escaped_char(msgbuf, ch);

            start = i + 1;
        }
    }

    if (i != start)
        lily_mb_add_slice(msgbuf, str, start, i);

    /* Add a terminating \0 so that the msgbuf is always \0 terminated. */
    if (is_bytestring)
        lily_mb_add_char(msgbuf, '\0');
}

void lily_free_msgbuf(lily_msgbuf *msgbuf)
{
    lily_free(msgbuf->message);
    lily_free(msgbuf);
}

/* This allows getting the contents without knowing the struct. */
const char *lily_mb_get(lily_msgbuf *msgbuf)
{
    return msgbuf->message;
}

void lily_mb_add(lily_msgbuf *msgbuf, const char *str)
{
    int len = strlen(str);

    if ((msgbuf->pos + len + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + len + 1);

    strcat(msgbuf->message, str);
    msgbuf->pos += len;
}

static void add_bytestring(lily_msgbuf *msgbuf, lily_string_val *sv)
{
    add_escaped_raw(msgbuf, 1, sv->string, sv->size);
}

/* Add a safe version of a \0 terminated string to a buffer. */
void lily_mb_escape_add_str(lily_msgbuf *msgbuf, const char *str)
{
    /* Both callers (vm's KeyError and msgbuf's interpolation) want the string
       to be quoted, so do it for them. */
    lily_mb_add_char(msgbuf, '"');
    add_escaped_raw(msgbuf, 0, str, strlen(str));
    lily_mb_add_char(msgbuf, '"');
}

/* Add a slice of text (start to stop) to the msgbuf. The slice does not need to
   be \0 terminated. However, the result will be \0 terminated. */
void lily_mb_add_slice(lily_msgbuf *msgbuf, const char *text,
        int start, int stop)
{
    int range = (stop - start);

    if ((msgbuf->pos + range + 1) > msgbuf->size)
        resize_msgbuf(msgbuf, msgbuf->pos + range + 1);

    memcpy(msgbuf->message + msgbuf->pos, text + start, range);
    msgbuf->pos += range;
    msgbuf->message[msgbuf->pos] = '\0';
}

void lily_mb_add_char(lily_msgbuf *msgbuf, char c)
{
    char ch_buf[2] = {c, '\0'};

    lily_mb_add(msgbuf, ch_buf);
}

static void add_boolean(lily_msgbuf *msgbuf, int b)
{
    if (b == 0)
        lily_mb_add(msgbuf, "false");
    else
        lily_mb_add(msgbuf, "true");
}

void lily_mb_add_byte(lily_msgbuf *msgbuf, uint8_t i)
{
    char buf[64];
    char ch = (char)i;
    char esc_ch = get_escape(ch);

    if (esc_ch)
        sprintf(buf, "'\\%c'", esc_ch);
    else if (isprint(ch))
        sprintf(buf, "'%c'", ch);
    else
        sprintf(buf, "'\\%03d'", ch);

    lily_mb_add(msgbuf, buf);
}

void lily_mb_add_int(lily_msgbuf *msgbuf, int i)
{
    char buf[64];
    sprintf(buf, "%d", i);

    lily_mb_add(msgbuf, buf);
}

void add_double(lily_msgbuf *msgbuf, double d)
{
    char buf[64];
    sprintf(buf, "%g", d);

    lily_mb_add(msgbuf, buf);
}

/* This erases what the msgbuf currently holds. */
void lily_mb_flush(lily_msgbuf *msgbuf)
{
    msgbuf->pos = 0;
    msgbuf->message[0] = '\0';
}

static void add_type(lily_msgbuf *msgbuf, lily_type *type)
{
    lily_mb_add(msgbuf, type->cls->name);

    if (type->cls->id == LILY_FUNCTION_ID) {
        if (type->generic_pos) {
            int i;
            char ch = 'A';
            lily_mb_add(msgbuf, "[");
            for (i = 0;i < type->generic_pos - 1;i++, ch++) {
                lily_mb_add_char(msgbuf, ch);
                lily_mb_add(msgbuf, ", ");
            }

            lily_mb_add_char(msgbuf, ch);
            lily_mb_add(msgbuf, "](");
        }
        else
            lily_mb_add(msgbuf, " (");

        if (type->subtype_count > 1) {
            int i;

            for (i = 1;i < type->subtype_count - 1;i++) {
                add_type(msgbuf, type->subtypes[i]);
                lily_mb_add(msgbuf, ", ");
            }

            if (type->flags & TYPE_IS_VARARGS) {
                /* Varargs are written as 'type ...', but internally are
                   actually 'list[type] ...'. This writes them as they would
                   have been written in (the extra ->subtypes[0] grabs the type
                   within the list. */
                add_type(msgbuf, type->subtypes[i]->subtypes[0]);
                lily_mb_add(msgbuf, "...");
            }
            else
                add_type(msgbuf, type->subtypes[i]);
        }
        if (type->subtypes[0] == lily_unit_type)
            lily_mb_add(msgbuf, ")");
        else {
            lily_mb_add(msgbuf, " => ");
            add_type(msgbuf, type->subtypes[0]);
            lily_mb_add(msgbuf, ")");
        }
    }
    else if (type->cls->generic_count != 0) {
        int i;
        int is_optarg = type->cls->id == LILY_OPTARG_ID;

        if (is_optarg == 0)
            lily_mb_add(msgbuf, "[");

        for (i = 0;i < type->subtype_count;i++) {
            add_type(msgbuf, type->subtypes[i]);
            if (i != (type->subtype_count - 1))
                lily_mb_add(msgbuf, ", ");
        }

        if (is_optarg == 0)
            lily_mb_add(msgbuf, "]");
    }
}

void lily_mb_add_fmt_va(lily_msgbuf *msgbuf, const char *fmt,
        va_list var_args)
{
    char buffer[128];
    int i, len, text_start;

    text_start = 0;
    len = strlen(fmt);

    for (i = 0;i < len;i++) {
        char c = fmt[i];
        if (c == '%') {
            if (i + 1 == len)
                break;

            if (i != text_start)
                lily_mb_add_slice(msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];

            if (c == 's') {
                char *str = va_arg(var_args, char *);
                lily_mb_add(msgbuf, str);
            }
            else if (c == 'd') {
                int d = va_arg(var_args, int);
                lily_mb_add_int(msgbuf, d);
            }
            else if (c == 'c') {
                char ch = va_arg(var_args, int);
                lily_mb_add_char(msgbuf, ch);
            }
            else if (c == 'p') {
                void *p = va_arg(var_args, void *);
                snprintf(buffer, 128, "%p", p);
                lily_mb_add(msgbuf, buffer);
            }
            else if (c == '%')
                lily_mb_add_char(msgbuf, '%');

            text_start = i+1;
        }
        /* ^ is for the msgbuf's custom arguments so they stand out. */
        else if (c == '^') {
            if (i != text_start)
                lily_mb_add_slice(msgbuf, fmt, text_start, i);

            i++;
            c = fmt[i];
            if (c == 'T') {
                lily_type *type = va_arg(var_args, lily_type *);
                add_type(msgbuf, type);
            }

            text_start = i+1;
        }
    }

    if (i != text_start)
        lily_mb_add_slice(msgbuf, fmt, text_start, i);
}

void lily_mb_add_fmt(lily_msgbuf *msgbuf, const char *fmt, ...)
{
    va_list var_args;
    va_start(var_args, fmt);
    lily_mb_add_fmt_va(msgbuf, fmt, var_args);
    va_end(var_args);
}

const char *lily_mb_sprintf(lily_msgbuf *msgbuf, const char *fmt, ...)
{
    lily_mb_flush(msgbuf);

    va_list var_args;
    va_start(var_args, fmt);
    lily_mb_add_fmt_va(msgbuf, fmt, var_args);
    va_end(var_args);

    return msgbuf->message;
}

typedef struct tag_ {
    struct tag_ *prev;
    lily_raw_value raw;
} tag;

static void add_value_to_msgbuf(lily_vm_state *, lily_msgbuf *, tag *,
        lily_value *);

static void add_list_like(lily_vm_state *vm, lily_msgbuf *msgbuf, tag *t,
        lily_value *v, const char *prefix, const char *suffix)
{
    int count, i;
    lily_value **values;

    if (v->class_id == LILY_LIST_ID ||
        v->class_id == LILY_TUPLE_ID) {
        values = v->value.list->elems;
        count = v->value.list->num_values;
    }
    else {
        values = v->value.instance->values;
        count = v->value.instance->num_values;
    }

    lily_mb_add(msgbuf, prefix);

    /* This is necessary because num_values is unsigned. */
    if (count != 0) {
        for (i = 0;i < count - 1;i++) {
            add_value_to_msgbuf(vm, msgbuf, t, values[i]);
            lily_mb_add(msgbuf, ", ");
        }
        if (i != count)
            add_value_to_msgbuf(vm, msgbuf, t, values[i]);
    }

    lily_mb_add(msgbuf, suffix);
}

static void add_value_to_msgbuf(lily_vm_state *vm, lily_msgbuf *msgbuf,
        tag *t, lily_value *v)
{
    if (v->flags & VAL_IS_GC_TAGGED) {
        tag *tag_iter = t;
        while (tag_iter) {
            /* Different containers may hold the same underlying values, so make
               sure to NOT test the containers. */
            if (memcmp(&tag_iter->raw, &v->value, sizeof(lily_raw_value)) == 0) {
                lily_mb_add(msgbuf, "[...]");
                return;
            }

            tag_iter = tag_iter->prev;
        }

        tag new_tag = {.prev = t, .raw = v->value};
        t = &new_tag;
    }

    if (v->class_id == LILY_BOOLEAN_ID)
        add_boolean(msgbuf, v->value.integer);
    else if (v->class_id == LILY_INTEGER_ID)
        lily_mb_add_int(msgbuf, v->value.integer);
    else if (v->class_id == LILY_BYTE_ID)
        lily_mb_add_byte(msgbuf, (uint8_t) v->value.integer);
    else if (v->class_id == LILY_DOUBLE_ID)
        add_double(msgbuf, v->value.doubleval);
    else if (v->class_id == LILY_STRING_ID)
        lily_mb_escape_add_str(msgbuf, v->value.string->string);
    else if (v->class_id == LILY_BYTESTRING_ID)
        add_bytestring(msgbuf, v->value.string);
    else if (v->class_id == LILY_FUNCTION_ID) {
        lily_function_val *fv = v->value.function;
        const char *builtin = "";
        const char *class_name = "";
        const char *separator = "";

        if (fv->code == NULL)
            builtin = "built-in ";

        if (fv->class_name) {
            class_name = fv->class_name;
            separator = ".";
        }

        lily_mb_add_fmt(msgbuf, "<%sfunction %s%s%s>", builtin, class_name,
                separator, fv->trace_name);
    }
    else if (v->class_id == LILY_DYNAMIC_ID)
        add_value_to_msgbuf(vm, msgbuf, t, v->value.dynamic->inner_value);
    else if (v->class_id == LILY_LIST_ID)
        add_list_like(vm, msgbuf, t, v, "[", "]");
    else if (v->class_id == LILY_TUPLE_ID)
        add_list_like(vm, msgbuf, t, v, "<[", "]>");
    else if (v->class_id == LILY_HASH_ID) {
        lily_hash_val *hv = v->value.hash;
        lily_mb_add_char(msgbuf, '[');
        int i, j;
        for (i = 0, j = 0;i < hv->num_bins;i++) {
            lily_hash_entry *entry = hv->bins[i];

            if (entry) {
                add_value_to_msgbuf(vm, msgbuf, t, entry->boxed_key);
                lily_mb_add(msgbuf, " => ");
                add_value_to_msgbuf(vm, msgbuf, t, entry->record);
                if (j != hv->num_entries - 1)
                    lily_mb_add(msgbuf, ", ");

                j++;
            }
        }
        lily_mb_add_char(msgbuf, ']');
    }
    else if (v->class_id == LILY_UNIT_ID)
        lily_mb_add(msgbuf, "unit");
    else if (v->class_id == LILY_FILE_ID) {
        lily_file_val *fv = v->value.file;
        const char *state = fv->inner_file ? "open" : "closed";
        lily_mb_add_fmt(msgbuf, "<%s file at %p>", state, fv);
    }
    else if (v->flags & VAL_IS_ENUM) {
        lily_instance_val *variant = v->value.instance;
        lily_class *variant_cls = vm->class_table[v->class_id];

        /* For scoped variants, render them how they're written. */
        if (variant_cls->parent->flags & CLS_ENUM_IS_SCOPED) {
            lily_mb_add(msgbuf, variant_cls->parent->name);
            lily_mb_add_char(msgbuf, '.');
        }

        lily_mb_add(msgbuf, variant_cls->name);
        if (variant != NULL)
            add_list_like(vm, msgbuf, t, v, "(", ")");
    }
    else {
        lily_class *cls = vm->class_table[v->class_id];
        const char *package_name = "";
        const char *separator = "";

        lily_mb_add_fmt(msgbuf, "<%s%s%s at %p>", package_name, separator,
                cls->name, v->value.generic);
    }
}

void lily_mb_add_value(lily_msgbuf *msgbuf, lily_vm_state *vm,
        lily_value *value)
{
    if (value->class_id == LILY_STRING_ID)
        lily_mb_add(msgbuf, value->value.string->string);
    else
        add_value_to_msgbuf(vm, msgbuf, NULL, value);
}
