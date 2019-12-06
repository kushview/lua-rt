/*
Copyright 2019 Michael Fisher <mfisher@kushview.net>

Permission to use, copy, modify, and/or distribute this software for any 
purpose with or without fee is hereby granted, provided that the above 
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, 
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM 
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR 
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR 
PERFORMANCE OF THIS SOFTWARE.
*/

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "luainc.h"
#include "util.h"
#include "lrt/lrt.h"

typedef union _midi_data_t {
    uint8_t* data;
    uint8_t bytes [sizeof (uint8_t*)];
} midi_data_t;

struct lrt_midi_message_impl_t {
    midi_data_t data;
    bool allocated;
    lua_Integer size;
    lua_Number  time;
};

struct lrt_midi_buffer_impl_t {
    uint8_t* data;
    lua_Integer size;
    lua_Integer used;
};

typedef struct lrt_midi_message_impl_t MidiMessage;
typedef struct lrt_midi_buffer_impl_t  MidiBuffer;

static void
lrt_midi_message_init (lrt_midi_message_t* msg) {
    msg->allocated = false;
    for (size_t i = 0; i < sizeof (uint8_t*); ++i)
        msg->data.bytes[i] = 0x00;
    msg->time = 0.0;
    msg->data.bytes[0] = 0xf0;
    msg->data.bytes[1] = 0xf7;
    msg->size = 2;
}

static uint8_t*
lrt_midi_message_data (lrt_midi_message_t* msg) {
    return msg->allocated ? msg->data.data : (uint8_t*) msg->data.bytes;
}

static lrt_midi_buffer_t* 
lrt_midi_buffer_init (lrt_midi_buffer_t* buf, size_t size) {
    buf->used = 0;

    if (size > 0) {
        buf->size = size;
        buf->data = malloc (buf->size);
    } else {
        buf->size = 0;
        buf->data = NULL;
    }
   
    return buf;
}

lrt_midi_buffer_t* 
lrt_midi_buffer_new (lua_State* L, size_t size) {
    size = MAX(0, size);
    MidiBuffer* buf = lua_newuserdata (L, sizeof (MidiBuffer));
    lrt_midi_buffer_init (buf, size);
    luaL_getmetatable (L, LRT_MT_MIDI_BUFFER);
    lua_setmetatable (L, -2);
    return buf;
}

static void
lrt_midi_buffer_free_data (lrt_midi_buffer_t* buf) {
    if (buf->data != NULL) {
        free (buf->data);
        buf->data = NULL;
    }
    buf->size = buf->used = 0;
}

static void 
lrt_midi_buffer_free (lrt_midi_buffer_t* buf) {
    lrt_midi_buffer_free_data (buf);
    free (buf);
}

void 
lrt_midi_buffer_insert (lrt_midi_buffer_t* buf, 
                        uint8_t*           bytes, 
                        size_t             len, 
                        int                frame)
{
    const int32_t iframe = (int32_t) frame;
    const int16_t ulen   = (uint16_t) len;
    const size_t needed = len + sizeof (int32_t) + sizeof (uint16_t);
    if (buf->size < buf->used + needed) {
        buf->data = realloc (buf->data, buf->size + needed);
        buf->size += needed;
    }

    uint8_t* iter = buf->data;
    uint8_t* end  = buf->data + buf->used;
    while (iter < end) {
        int32_t evfr = *(int32_t*) iter;
        
        if (evfr >= iframe) {
            memmove (iter + needed, iter, end - iter);
            break;
        }

        iter += (sizeof(int32_t) + sizeof(uint16_t) + *(uint16_t*)(iter + sizeof (int32_t)));
    }

    memcpy (iter, &iframe, sizeof (int32_t));
    iter += sizeof (int32_t);
    memcpy (iter, &ulen, sizeof (uint16_t));
    iter += sizeof (uint16_t);
    memcpy (iter, bytes, len);
    buf->used += needed;
}

lrt_midi_buffer_iter_t 
lrt_midi_buffer_begin (lrt_midi_buffer_t* buf) {
    return buf->data;
}

lrt_midi_buffer_iter_t 
lrt_midi_buffer_end (lrt_midi_buffer_t* buf) {
    return buf->data + buf->used;
}

lrt_midi_buffer_iter_t 
lrt_midi_buffer_next (lrt_midi_buffer_t*     buf, 
                      lrt_midi_buffer_iter_t iter)
{
    iter += (sizeof(int32_t) + sizeof(uint16_t) + *(uint16_t*)(iter + sizeof (int32_t)));
    return (uint8_t*)iter <= buf->data + buf->used ? iter : buf->data + buf->used;
}

//=============================================================================
static int midimessage_update (lua_State* L) {
    MidiMessage* msg = lua_touserdata (L, 1);
    
    if (lua_gettop (L) >= 3) {
        uint8_t* data    = lua_touserdata (L, 2);
        lua_Integer size = lua_tointeger (L, 3);
        if (size > 0) {
            memcpy (lrt_midi_message_data (msg), data, size);
            msg->size = size;
        }
    }

    return 0;
}

static int midimessage_channel (lua_State* L) {
    MidiMessage* msg = lua_touserdata (L, 1);
    uint8_t* data = lrt_midi_message_data (msg);
    
    if ((data[0] & 0xf0) == (uint8_t) 0xf0) {
        lua_pushinteger (L, 0);
        return 1;
    }

    if (lua_gettop (L) >= 2) {
        lua_Integer channel = lua_tointeger (L, 2);
        assert (channel >= 1 && channel <= 16);
        data[0] = (data[0] & (uint8_t)0xf0) | (uint8_t)(channel - 1);
    }

    lua_pushinteger (L, (data[0] & 0xf) + 1);
    return 1;
}

static int midimessage_isnoteon (lua_State* L) {
    MidiMessage* msg = lua_touserdata (L, 1);
    uint8_t* data = msg->data.data;
    lua_pushboolean (L, (data[0] & 0xf0) == 0x90 && data[2] != 0);
    return 1;
}

static int midimessage_isnoteoff (lua_State* L) {
    MidiMessage* msg = lua_touserdata (L, 1);
    uint8_t* data = msg->data.data;
    lua_pushboolean (L, (data[0] & 0xf0) == 0x80);
    return 1;
}

//=============================================================================
static int midibuffer_gc (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    lrt_midi_buffer_free_data (buf);
    return 0;
}

static int midibuffer_insert (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    if (lua_gettop (L) == 3) {
        lua_Integer data = lua_tointeger (L, 3);
        lrt_midi_buffer_insert (buf, (uint8_t*)&data, 3, lua_tointeger (L, 2));
        lua_pushinteger (L, 3);
    } else {
        int size = (int)(lua_gettop (L) - 2);

        if (size > 1) {
            uint8_t data [size];
            int frame = lua_tointeger (L, 2);

            for (int i = 0; i < size; ++i) {
                data[i] = (uint8_t) lua_tointeger (L, i + 3);
            }

            lrt_midi_buffer_insert (buf, data, (size_t)size, frame);
        }

        lua_pushinteger (L, (lua_Integer) size);
    }
    return 1;
}

static int midibuffer_clear (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    buf->used = 0;
    return 0;
}

static int midibuffer_handle (lua_State* L) {
    lua_pushlightuserdata (L, lua_touserdata (L, 1));
    return 1;
}

static void lrt_midi_buffer_swap (lrt_midi_buffer_t* a, lrt_midi_buffer_t* b) {
    a->size = a->size + b->size;
    b->size = a->size - b->size;
    a->size = a->size - b->size;

    a->used = a->used + b->used;
    b->used = a->used - b->used;
    a->used = a->used - b->used;

    a->data = (uint8_t*)((uintptr_t)a->data + (uintptr_t)b->data);
    b->data = (uint8_t*)((uintptr_t)a->data - (uintptr_t)b->data);
    a->data = (uint8_t*)((uintptr_t)a->data - (uintptr_t)b->data);
}

//=============================================================================
static int midibuffer_capacity (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    lua_pushinteger (L, (lua_Integer) buf->size);
    return 1;
}

static int midibuffer_swap (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    
    switch (lua_type (L, 2)) {
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA: {
            lrt_midi_buffer_swap (buf, lua_touserdata (L, 2));
            break;
        }

        default: {
            return luaL_error (L, "unsupported type: %s", luaL_typename (L, 2));
            break;
        }
    }

    return 0;
}

static int midibuffer_iter_f (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, lua_upvalueindex (1));
    lrt_midi_buffer_iter_t iter = lua_touserdata (L, lua_upvalueindex (2));
    
    if (iter == lrt_midi_buffer_end (buf)) {
        lua_pushnil (L);
        return 1;
    }

    lua_pushlightuserdata (L, lrt_midi_buffer_next (buf, iter));
    lua_copy (L, -1, lua_upvalueindex (2));
    lua_pop (L, -1);

    lua_pushlightuserdata (L, lrt_midi_buffer_iter_data (iter));
    lua_pushinteger (L, lrt_midi_buffer_iter_size (iter));
    lua_pushinteger (L, lrt_midi_buffer_iter_frame (iter));
    
    return 3;
}

static int midibuffer_iter (lua_State* L) {
    MidiBuffer* buf = lua_touserdata (L, 1);
    lua_pushlightuserdata (L, buf);
    lua_pushlightuserdata (L, lrt_midi_buffer_begin (buf));
    lua_pushcclosure (L, &midibuffer_iter_f, 2);
    return 1;
}

//=============================================================================
static int f_msg3bytes (lua_State* L, uint8_t status) {
    lua_Integer block = 0;
    uint8_t* data = (uint8_t*) &block;
    data[0] = status | (uint8_t)(lua_tointeger (L, 1) - 1);
    data[1] = (uint8_t) lua_tointeger (L, 2);
    data[2] = (uint8_t) lua_tointeger (L, 3);
    lua_pushinteger (L, (lua_Integer) block);
    return 1;
}

static int f_controller (lua_State* L)  { return f_msg3bytes (L, 0xb0); }
static int f_noteon (lua_State* L)      { return f_msg3bytes (L, 0x80); }
static int f_noteoff (lua_State* L)     { return f_msg3bytes (L, 0x90); }

const static luaL_Reg midimessage_m[] = {
    { "update",     midimessage_update },
    { "channel",    midimessage_channel },
    { "isnoteon",   midimessage_isnoteon },
    { "isnoteoff",  midimessage_isnoteoff },
    { NULL, NULL }
};

const static luaL_Reg midibuffer_m[] = {
    { "__gc",       midibuffer_gc },
    { "insert",     midibuffer_insert },
    { "capacity",   midibuffer_capacity },
    { "clear",      midibuffer_clear },
    { "swap",       midibuffer_swap },
    { "iter",       midibuffer_iter },
    { NULL, NULL }
};

static int midimessage_new (lua_State* L) {
    MidiMessage* msg = lua_newuserdata (L, sizeof (MidiMessage));
    lrt_midi_message_init (msg);
    luaL_getmetatable (L, LRT_MT_MIDI_MESSAGE);
    lua_setmetatable (L, -2);
    return 1;
}

static int midibuffer_new (lua_State* L) {
    size_t size = (size_t)(lua_gettop(L) > 0 ? MAX(0, lua_tointeger (L, 1)) : 0);
    lrt_midi_buffer_new (L, size);
    return 1;
}

const static luaL_Reg midi_f[] = {
    { "Buffer",     midibuffer_new },
    { "Message",    midimessage_new },
    { "controller", f_controller },
    { "noteon",     f_noteon },
    { "noteoff",    f_noteoff },
    { NULL, NULL }
};

static const luaL_Reg midibuffer_mm[] = {
    { "__gc",  midibuffer_gc },
    { NULL, NULL }
};

static int midimessage_gc (lua_State* L) {
    MidiMessage* msg = lua_touserdata (L, 1);
    if (msg->allocated) {
        free (msg->data.data);
    }
    return 0;
}

static const luaL_Reg midimessage_mm[] = {
    { "__gc",  midimessage_gc },
    { NULL, NULL }
};

int luaopen_midi (lua_State* L) {
    luaL_newmetatable (L, LRT_MT_MIDI_BUFFER);
    // luaL_setfuncs (L, midibuffer_mm, 0);
    lua_pushvalue (L, -1);               /* duplicate the metatable */
    lua_setfield (L, -2, "__index");     /* mt.__index = mt */
    luaL_setfuncs (L, midibuffer_m, 0);
    lua_pop (L, -1);

    luaL_newmetatable (L, LRT_MT_MIDI_MESSAGE);
    // luaL_setfuncs (L, midimessage_mm, 0);
    lua_pushvalue (L, -1);               /* duplicate the metatable */
    lua_setfield (L, -2, "__index");     /* mt.__index = mt */
    luaL_setfuncs (L, midimessage_m, 0);
    lua_pop (L, -1);

    luaL_newlib (L, midi_f);
    return 1;
}
