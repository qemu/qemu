/* g-lru-cache.h
 *
 * Copyright (C) 2009 - Christian Hergert
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __G_LRU_CACHE_H__
#define __G_LRU_CACHE_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define G_TYPE_LRU_CACHE        (g_lru_cache_get_type ())
#define G_LRU_CACHE(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_LRU_CACHE, GLruCache))
#define G_LRU_CACHE_CONST(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_LRU_CACHE, GLruCache const))
#define G_LRU_CACHE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_TYPE_LRU_CACHE, GLruCacheClass))
#define G_IS_LRU_CACHE(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_TYPE_LRU_CACHE))
#define G_IS_LRU_CACHE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_TYPE_LRU_CACHE))
#define G_LRU_CACHE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_LRU_CACHE, GLruCacheClass))
#define G_LOOKUP_FUNC(func)             ((GLookupFunc)func)

typedef struct _GLruCache       GLruCache;
typedef struct _GLruCacheClass  GLruCacheClass;
typedef struct _GLruCachePrivate    GLruCachePrivate;

typedef gpointer (*GLookupFunc) (gpointer key, gpointer user_data);

struct _GLruCache
{
    GObject parent;
    
    GLruCachePrivate *priv;
};

struct _GLruCacheClass
{
    GObjectClass parent_class;
};

GType      g_lru_cache_get_type     (void) G_GNUC_CONST;

GLruCache* g_lru_cache_new          (GHashFunc      hash_func,
                                     GEqualFunc     key_equal_func,
                                     GCopyFunc      key_copy_func,
                                     GLookupFunc    retrieve_func,
                                     GDestroyNotify key_destroy_func,
                                     GDestroyNotify value_destroy_func,
                                     gpointer       user_data,
                                     GDestroyNotify user_destroy_func);

void       g_lru_cache_set_max_size (GLruCache *self, guint max_size);
guint      g_lru_cache_get_max_size (GLruCache *self);

guint      g_lru_cache_get_size     (GLruCache *self);

gpointer   g_lru_cache_get          (GLruCache *self, gpointer key);
void       g_lru_cache_evict        (GLruCache *self, gpointer key);
void       g_lru_cache_clear        (GLruCache *self);

gboolean   g_lru_cache_get_fast_get (GLruCache *self);
void       g_lru_cache_set_fast_get (GLruCache *self, gboolean fast_get);

G_END_DECLS

#endif /* __G_LRU_CACHE_H__ */
