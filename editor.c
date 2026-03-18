/*
  For the editor I'm using an immutable original buffer, an Add Buffer, and a Piece Table to track changes. At a later date it will support
  ctrl+zi/y for undo/redo

  Currently, a file can either be loaded from disk, or a new file can be created.

  The input_handler will buffer input and send it to here to be added to the document. The input_handler can also control the view via the screen system

  The screen system will request a subset of the current document for viewing, depending on what part of the file is currently in view

  This system will also handle the cursor position and handle where text is inserted/removed
 */
#include "editor.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t size;
} File_Buffer;

typedef enum { SRC_ORIGINAL, SRC_ADD } PieceSrc;

typedef struct {
    PieceSrc src; 
    size_t off;
    size_t len;
} Piece;

typedef struct {
    Piece *v;
    size_t n;
    size_t cap;
} PieceVec;

typedef struct {
    uint8_t *data;
    size_t used;
    size_t cap;
} Add_Buf;


typedef struct{
    size_t index;
    size_t in_off; //offset inside piece
} Locate;

typedef struct {
    // Original file data
    File_Buffer original_buf;
    // Additional text added by user this session
    Add_Buf add;

    // Current state of document is defined by this piece list
    PieceVec pieces;

    // Total logical size of document in bytes
    size_t len;

    // Where in the document the cursor currently is
    size_t cur_pos;
} Doc;

// Forward declare internal functions
int file_open(const char *filename, File_Buffer *buf);
int file_save();

static Locate locate_pos(const PieceVec *pv, size_t pos);
static int ensure_piece_cap(PieceVec *pv, size_t extra);
static void vec_replace(PieceVec *pv, size_t idx, const Piece *repl, size_t k);
int doc_insert_piece(PieceVec *pv, size_t pos, Piece ins);
static int add_reserve(Add_Buf *a, size_t extra);
static int add_append(Add_Buf *a, const uint8_t *bytes, size_t len, size_t *out_off);


// Define the document
Doc *doc;

// Should really just overload this into two functions
int editor_init(int new_file_flag) {
    // create an unchangeable file buffer of size 0
    if (new_file_flag == 1) {
        doc = malloc(sizeof(Doc));

        if (!doc) return -1;

        doc->original_buf.data = NULL;
        doc->original_buf.size = 0;
    } else {
        // Ask for filename here
        char *filename = "newfile.txt";
        int file_open_result = file_open(filename, &doc->original_buf);
        if (file_open_result != 0) {
            printf("Error opening file\n");
            return -1;
        }
    }

    doc->add.data = NULL;
    doc->add.used = 0;
    doc->add.cap = 0;

    doc->pieces.v = NULL;
    doc->pieces.cap = 0;

    doc->len = doc->original_buf.size;

    doc->cur_pos = 0;
    
    return 0;
}

void editor_destroy () {
    free(doc->original_buf.data);
    free(doc->add.data);
    free(doc->pieces.v);
    free(doc);
}

int doc_insert_bytes(const uint8_t *bytes, size_t len) {
    size_t add_off;
    if (add_append(&doc->add, bytes, len, &add_off) != 0) return -1;

    Piece ins = { .src = SRC_ADD, .off = add_off, .len = len};

    if (doc_insert_piece(&doc->pieces, doc->cur_pos, ins) != 0) return -1;

    doc->len += len;
    doc->cur_pos += len;
    return 0;
}

size_t get_cursor_pos() {
    return doc->cur_pos;
} 


// Internal Functions
int file_open(const char *filename, File_Buffer *buf) {
    FILE *file_ptr = fopen(filename, "rb");

    fseek(file_ptr, 0, SEEK_END);
    long size = ftell(file_ptr);
    if (size < 0) {
        fclose(file_ptr);
        return -1;
    }
    rewind(file_ptr);

    buf->data = malloc((size_t)size);
    if (!buf->data && size != 0) {
        fclose(file_ptr);
        return -2;
    }

    size_t read = fread(buf->data, 1, (size_t)size, file_ptr);
    fclose(file_ptr);

    if (read != (size_t)size) {
        free(buf->data);
        buf->data = NULL;
        return -3;
    }

    buf->size = read;
    
    return 0; 
}

int file_save() {
    return 0;
}

// Used to find a location in an existing piece
static Locate locate_pos(const PieceVec *pv, size_t pos) {
    size_t cur = 0;
    for (size_t i = 0; i < pv->n; i++) {
        size_t next = cur + pv->v[i].len;
        if (pos <= next) { // Position at end of piece is allowed
            return (Locate){ .index = i, .in_off = pos - cur };
        }
        cur = next;
    }
    // position at end of document: insert after last piece
    return (Locate){ .index = pv->n, .in_off = 0 };
}

static int ensure_piece_cap(PieceVec *pv, size_t extra) {
    if (pv->n + extra <= pv->cap) return 0;
    size_t new_cap = pv->cap ? pv->cap : 16;
    while (new_cap < pv->n + extra) new_cap *= 2;
    void *p = realloc(pv->v, new_cap * sizeof(Piece));
    if (!p) return -1;
    pv->v = p;
    pv->cap = new_cap;
    return 0;
}

// replace pv->v[idx] with repl[0..k-1]
static void vec_replace(PieceVec *pv, size_t idx, const Piece *repl, size_t k) {
    size_t tail = pv->n - idx -1;
    if (tail > 0) {
        // Remove 1, insert k => net change = (k - 1)
        if (k > 1) {
            memmove(&pv->v[idx + k], &pv->v[idx + 1], (pv->n - idx -1) * sizeof(Piece));
        } else if (k == 0) {
            memmove(&pv->v[idx], &pv->v[idx + 1], (pv->n - idx - 1) * sizeof(Piece));
        }
    }

    for (size_t i = 0; i < k; i++) pv->v[idx + 1] = repl[i];
    pv->n = pv->n - 1 + k;
}

int doc_insert_piece(PieceVec *pv, size_t pos, Piece ins) {
    // pos can be 0 to doc_len
    Locate loc = locate_pos(pv, pos);

    // Inserting at end? Just append
    if (loc.index == pv->n) {
        if (ensure_piece_cap(pv, 1) != 0) return -1;
        pv->v[pv->n++] = ins;
        return 0;
    }

    Piece p = pv->v[loc.index];

    Piece out[3];
    size_t k = 0;

    // left split
    if (loc.in_off > 0) {
        out[k++] = (Piece){ .src = p.src, .off = p.off, .len = loc.in_off };
    }

    // inserted piece
    out[k++] = ins;

    // right split
    if (loc.in_off < p.len) {
        out[k++] = (Piece){ .src = p.src, .off = p.off + loc.in_off, .len = p.len - loc.in_off };
    }

    if (ensure_piece_cap(pv, (k > 1) ? (k - 1) : 0) != 0) return -1;
    vec_replace(pv, loc.index, out, k);
    return 0;
}

static int add_reserve(Add_Buf *a, size_t extra) {
    if (a->used + extra <= a->cap) return 0;

    size_t new_cap = a->cap ? a->cap : 4096;
    while (new_cap < a->used + extra) new_cap *= 2;

    uint8_t *p = (uint8_t *)realloc(a->data, new_cap);
    if (!p) return -1;

    a->data = p;
    a->cap = new_cap;
    return 0;
}

static int add_append(Add_Buf *a, const uint8_t *bytes, size_t len, size_t *out_off) {
    if (len == 0) { *out_off = a->used; return 0; }
    if (add_reserve(a, len) != 0) return -1;

    size_t off = a->used;
    memcpy(a->data + a->used, bytes, len);
    a->used += len;

    *out_off = off;
    return 0;
}

static const uint8_t *piece_base_ptr(const Doc *doc, PieceSrc src) {
    return (src == SRC_ORIGINAL) ? doc->original_buf.data : doc->add.data;
}

// Copies up to out_cap bytes from doc starting at start_pos.
// Returns bytes copied.
size_t doc_build_slice(size_t start_pos, uint8_t *out, size_t out_cap)
{
    if (!doc || !out || out_cap == 0) return 0;
    if (start_pos >= doc->len) return 0;

    size_t out_n = 0;
    size_t logical_cur = 0;

    for (size_t i = 0; i < doc->pieces.n && out_n < out_cap; i++) {
        Piece p = doc->pieces.v[i];
        size_t piece_end = logical_cur + p.len;

        // Skip pieces before start_pos
        if (piece_end <= start_pos) {
            logical_cur = piece_end;
            continue;
        }

        // Start offset within this piece
        size_t in_piece_off = (start_pos > logical_cur) ? (start_pos - logical_cur) : 0;
        size_t available = p.len - in_piece_off;
        size_t take = out_cap - out_n;
        if (take > available) take = available;

        const uint8_t *base = piece_base_ptr(doc, p.src);
        memcpy(out + out_n, base + p.off + in_piece_off, take);

        out_n += take;
        start_pos += take;
        logical_cur = piece_end;
    }

    return out_n;
}
