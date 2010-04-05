#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <gtk/gtk.h>
#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>

/**/
#define __LONG_TYPE_64
#include "lazy_passthrough_internal.h"

/**/
/* #define HAVE_UI_DEBUG */
/* #define HAVE_SERVER_DEBUG */

#ifdef HAVE_UI_DEBUG
# define UI_DEBUG(args...) do {                 \
                g_log (G_LOG_DOMAIN,            \
                       G_LOG_LEVEL_DEBUG,       \
                       args);                   \
        } while (0)
#else
# define UI_DEBUG(args...)
#endif /* HAVE_UI_DEBUG */

#ifdef HAVE_SERVER_DEBUG
# define SERVER_DEBUG(args...) do {              \
                g_log (G_LOG_DOMAIN,             \
                       G_LOG_LEVEL_DEBUG,        \
                       args);                    \
        } while (0)
#else
# define SERVER_DEBUG(args...)
#endif /* HAVE_SERVER_DEBUG */

#define SERVER_WARN(args...) do {               \
                g_log (G_LOG_DOMAIN,            \
                       G_LOG_LEVEL_WARN,        \
                       args);                   \
        } while (0)
#define SERVER_ERROR(args...) do {              \
                g_log (G_LOG_DOMAIN,            \
                       G_LOG_LEVEL_ERROR,       \
                       args);                   \
        } while (0)

#define WINWIDTH (1280)
#define WINHEIGHT (720)

#define DEFAULT_BUFFER_PATH "/tmp/rootfs/tmp"

/**/
gchar *path_to_buffers = DEFAULT_BUFFER_PATH;

/**/
typedef struct
{
        gchar *filename;
        gpointer ptr;
        gint fd;

        guint id;
        gint  width;
        gint  height;
        gint  bpp;
} emu_buffer_t;

guint emu_buffer_get_size (emu_buffer_t *buffer);

void
emu_buffer_free (emu_buffer_t *buffer)
{
        g_return_if_fail (buffer != NULL);

        if (buffer->ptr != NULL && buffer->ptr != MAP_FAILED)
                munmap (buffer->ptr, emu_buffer_get_size (buffer));

        if (buffer->fd >= 0)
                close (buffer->fd);

        if (buffer->filename)
                g_free (buffer->filename);

        g_free (buffer);
}

emu_buffer_t *
emu_buffer_new (guint id, gint width, gint height, gint bpp)
{
        emu_buffer_t *buffer;

        g_return_val_if_fail (width >= 0 && height >= 0 && bpp >= 0, NULL);

        buffer = g_new0 (emu_buffer_t, 1);

        g_return_val_if_fail (buffer != NULL, NULL);

        buffer->filename = g_strdup_printf ("%s/%x", path_to_buffers, id);
        buffer->id = id;
        buffer->width = width;
        buffer->height = height;
        buffer->bpp = bpp;

        buffer->fd = open (buffer->filename, O_CREAT | O_RDWR,
                           S_IRUSR | S_IWUSR | S_IRGRP |
                           S_IWGRP | S_IROTH | S_IWOTH);
        if (buffer->fd < 0)
        {
                SERVER_ERROR ("Cannot open %s : %s",
                              buffer->filename, strerror (errno));
                goto error;
        }

        if (lseek (buffer->fd,
                   buffer->width * buffer->height * buffer->bpp,
                   SEEK_SET) == -1)
        {
                SERVER_ERROR ("Cannot lseek in %s : %s",
                              buffer->filename, strerror (errno));
                goto error;
        }

        /* Unsure we can mmap the file... */
        if (write (buffer->fd, &buffer, 4) != 4)
        {
                SERVER_ERROR ("Cannot write in %s : %s",
                              buffer->filename, strerror (errno));
                goto error;
        }

        buffer->ptr = mmap (NULL,
                            width * height * bpp,
                            PROT_READ, MAP_SHARED,
                            buffer->fd, 0);
        if (buffer->ptr == NULL ||
            buffer->ptr == MAP_FAILED)
        {
                SERVER_ERROR ("Cannot mmap %s : %s",
                              buffer->filename, strerror (errno));
                goto error;
        }

        return buffer;

error:
        emu_buffer_free (buffer);

        return NULL;
}

guint
emu_buffer_get_size (emu_buffer_t *buffer)
{
        g_return_val_if_fail (buffer != NULL, 0);

        return buffer->width * buffer->height * buffer->bpp;
}

gint
emu_buffer_compare (emu_buffer_t *b1, emu_buffer_t *b2)
{
        return b2->id - b1->id;
}

/**/
typedef struct
{
        GList *buffers;
        guint  buffer_index;
        guint  nb_buffers;
        guint  nb_max_buffers;
} emu_buffer_pool_t;

void
emu_buffer_pool_free (emu_buffer_pool_t *pool)
{
        g_return_if_fail (pool != NULL);

        g_list_foreach (pool->buffers, (GFunc) emu_buffer_free, NULL);
        g_list_free (pool->buffers);

        g_free (pool);
}

emu_buffer_pool_t *
emu_buffer_pool_new (guint size)
{
        emu_buffer_pool_t *pool;

        g_return_val_if_fail (size > 0, NULL);

        pool = g_new0 (emu_buffer_pool_t, 1);

        g_return_val_if_fail (pool != NULL, NULL);

        pool->nb_max_buffers = size;

        return pool;
}

emu_buffer_t *
emu_buffer_pool_find_buffer (emu_buffer_pool_t *pool, guint id)
{
        emu_buffer_t  buffer;
        GList        *item;

        g_return_val_if_fail (pool != NULL, NULL);

        buffer.id = id;
        item = g_list_find_custom (pool->buffers, &buffer,
                                   (GCompareFunc) emu_buffer_compare);

        if (item)
        {
                if (item != pool->buffers)
                {
                        /* LRU :) */
                        pool->buffers = g_list_remove_link (pool->buffers, item);

                        pool->buffers->prev = item;
                        item->next = pool->buffers;
                        pool->buffers = item;
                }

                return (emu_buffer_t *) item->data;
        }

        return NULL;
}

emu_buffer_t *
emu_buffer_pool_add_buffer (emu_buffer_pool_t *pool,
                            gint width, gint height,
                            gint bpp)
{
        emu_buffer_t *buffer;

        g_return_val_if_fail (pool != NULL, NULL);

        buffer = emu_buffer_new (pool->buffer_index++, width, height, bpp);

        g_return_val_if_fail (buffer != NULL, NULL);

        if (pool->nb_buffers >= pool->nb_max_buffers)
        {
                GList *last_item;
                emu_buffer_t *last_buffer;

                last_item = g_list_last (pool->buffers);
                last_buffer = (emu_buffer_t *) last_item->data;

                pool->buffers = g_list_delete_link (pool->buffers, last_item);
                emu_buffer_free (last_buffer);
        }
        else
                pool->nb_buffers++;

        pool->buffers = g_list_insert_before (pool->buffers,
                                              pool->buffers,
                                              buffer);

        return buffer;
}

void
emu_buffer_pool_del_buffer (emu_buffer_pool_t *pool, guint id)
{
        emu_buffer_t  dbuffer;
        GList        *item;

        g_return_if_fail (pool != NULL);

        dbuffer.id = id;
        item = g_list_find_custom (pool->buffers, &dbuffer,
                                   (GCompareFunc) emu_buffer_compare);

        if (item)
        {
                emu_buffer_t *buffer;

                buffer = (emu_buffer_t *) item->data;
                pool->buffers = g_list_delete_link (pool->buffers, item);
                emu_buffer_free (buffer);

                pool->nb_buffers--;
        }
}

/**/
typedef struct
{
        gint id;

        emu_buffer_t *buffer;

        gint width, height;

        GdkRectangle src;
        GdkRectangle dst;

        ClutterActor *actor;
} emu_layer_t;

void
emu_layer_free (emu_layer_t *layer)
{
        if (!layer)
                return;

        if (layer->actor)
        {
                layer->actor = NULL;
        }

        g_free (layer);
}

emu_layer_t *
emu_layer_new (gint id, gint width, gint height)
{
        emu_layer_t *layer;

        layer = g_new0 (emu_layer_t, 1);

        g_return_val_if_fail (layer != NULL, NULL);

        layer->id = id;
        layer->width = width;
        layer->height = height;
        /* layer->size = 4 * width * height; */

        /* layer->fd = -1; */

        /* layer->src.x = sx; */
        /* layer->src.y = sy; */
        /* layer->src.width = sw; */
        /* layer->src.height = sh; */

        /* layer->dst.x = dx; */
        /* layer->dst.y = dy; */
        /* layer->dst.width = dw; */
        /* layer->dst.height = dh; */

        layer->actor = clutter_texture_new ();
        clutter_actor_set_opacity (layer->actor, 0xff);
        clutter_actor_show (layer->actor);

        return layer;

        return NULL;
}

void
emu_layer_set_viewport_input (emu_layer_t *layer,
                              gint x, gint y,
                              gint width, gint height)
{
        g_return_if_fail (layer != NULL);

        layer->src.x = x;
        layer->src.y = y;
        layer->src.width = width;
        layer->src.height = height;

        clutter_actor_set_clip (layer->actor,
                                layer->src.x, layer->src.y,
                                layer->src.width, layer->src.height);
}

void
emu_layer_set_viewport_output (emu_layer_t *layer,
                               gint x, gint y,
                               gint width, gint height)
{
        g_return_if_fail (layer != NULL);

        layer->dst.x = x;
        layer->dst.y = y;
        layer->dst.width = width;
        layer->dst.height = height;

        clutter_actor_set_position (layer->actor,
                                    layer->dst.x,
                                    layer->dst.y);
        clutter_actor_set_size (layer->actor,
                                layer->dst.width,
                                layer->dst.height);
}

void
emu_layer_set_opacity (emu_layer_t *layer, guint8 opacity)
{
        g_return_if_fail (layer != NULL);

        clutter_actor_set_opacity (layer->actor, opacity);
}

void
emu_layer_set_buffer (emu_layer_t *layer, emu_buffer_t *buffer)
{
        /* gboolean  buffer_changed = FALSE; */

        g_return_if_fail (layer != NULL || buffer != NULL);

        UI_DEBUG ("buffer in %s", buffer->filename);

        if (layer->buffer != buffer)
        {
                UI_DEBUG ("changing buffer ptr in clutter");
                clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (layer->actor),
                                                   buffer->ptr,
                                                   TRUE,
                                                   layer->width,
                                                   layer->height,
                                                   4 * layer->width,
                                                   4,
                                                   CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                                   NULL);
        }

        clutter_actor_queue_redraw (layer->actor);
}

/**/
typedef struct
{
        emu_buffer_pool_t *buffer_pool;
        GList             *layers;
        ClutterStage      *stage;
} emu_mixer_t;

static void
emu_mixer_free_layer (emu_layer_t *layer,
                      emu_mixer_t *mixer)
{
        clutter_container_remove_actor (CLUTTER_CONTAINER (mixer->stage),
                                        layer->actor);
        layer->actor = NULL;
        emu_layer_free (layer);
}

void
emu_mixer_free (emu_mixer_t *mixer)
{
        g_return_if_fail (mixer != NULL);

        g_list_foreach (mixer->layers,
                        (GFunc) emu_mixer_free_layer,
                        mixer);
        g_list_free (mixer->layers);

        emu_buffer_pool_free (mixer->buffer_pool);

        g_free (mixer);
}

emu_mixer_t *
emu_mixer_new (ClutterStage *stage)
{
        emu_mixer_t *mixer;

        g_return_val_if_fail (stage != NULL, NULL);

        mixer = g_new0 (emu_mixer_t, 1);

        g_return_val_if_fail (mixer != NULL, NULL);

        mixer->stage = stage;

        mixer->buffer_pool = emu_buffer_pool_new (10);
        if (mixer->buffer_pool == NULL)
                goto error;

        return mixer;

error:
        emu_mixer_free (mixer);

        return NULL;
}

static gint
emu_mixer_compare_layer (emu_layer_t *layer1,
                         emu_layer_t *layer2)
{
        return layer2->id - layer1->id;
}

/*
  Returns: negative value if error, 0 if added for the first time, 1
  if already added.
*/
int
emu_mixer_add_layer (emu_mixer_t *mixer, emu_layer_t *layer)
{
        GList *e;

        g_return_val_if_fail (mixer != NULL, -1);
        g_return_val_if_fail (layer != NULL, -1);

        e = g_list_find_custom (mixer->layers,
                                layer,
                                (GCompareFunc) emu_mixer_compare_layer);

        if (e)
        {
                UI_DEBUG ("layer already added...");
                return 1;
        }

        mixer->layers = g_list_append (mixer->layers, layer);
        clutter_container_add_actor (CLUTTER_CONTAINER (mixer->stage),
                                     layer->actor);

        return 0;
}

void
emu_mixer_del_layer (emu_mixer_t *mixer, gint id)
{
        GList *e;
        emu_layer_t l;

        g_return_if_fail (mixer != NULL);

        l.id = id;
        e = g_list_find_custom (mixer->layers, &l,
                                (GCompareFunc) emu_mixer_compare_layer);

        if (e)
        {
                emu_layer_t *layer = e->data;

                mixer->layers = g_list_delete_link (mixer->layers, e);
                emu_mixer_free_layer (layer, mixer);
        }
}

emu_layer_t *
emu_mixer_find_layer (emu_mixer_t *mixer, gint id)
{
        GList *e;
        emu_layer_t l;

        g_return_val_if_fail (mixer != NULL, NULL);

        l.id = id;
        e = g_list_find_custom (mixer->layers, &l,
                                (GCompareFunc) emu_mixer_compare_layer);

        if (e)
                return (emu_layer_t *) e->data;

        return NULL;
}

guint connection_id = 0;

static gboolean
server_input_send_result (GIOChannel *source, void *result, guint length)
{
        gsize transfereddata = 0;
        /* int i; */

        if ((g_io_channel_write (source,
                                 (gchar *) result, length,
                                 &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != length))
        {
                SERVER_ERROR ("Cannot ack operation...");
                return FALSE;
        }

        return TRUE;
}

static gboolean
server_input_addlayer (GIOChannel *source,
                       emu_mixer_t *mixer)
{
        lazy_operation_addlayer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);
        lazy_operation_addlayer_res_t res_operation;
        emu_layer_t *layer;
        emu_buffer_t *buffer;

        res_operation.result = LAZY_OPERATION_RESULT_FAILURE;

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot addlayer operation...");
                return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
        }

        SERVER_DEBUG ("add layer %ix%i@%ix%i -> %ix%i@%ix%i - buffer=%i",
                      operation.src.w, operation.src.h,
                      operation.src.x, operation.src.y,
                      operation.dst.w, operation.dst.h,
                      operation.dst.x, operation.dst.y,
                      operation.buffer_id);


        buffer = emu_buffer_pool_find_buffer (mixer->buffer_pool, operation.buffer_id);
        if (buffer == NULL)
        {
                SERVER_ERROR ("Cannot find buffer %i in mixer...",
                              operation.buffer_id);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        if (operation.src.x >= buffer->width ||
            operation.src.y >= buffer->height ||
            (operation.src.x + operation.src.w) > buffer->width ||
            (operation.src.y + operation.src.h) > buffer->height)
        {
                SERVER_ERROR ("Input viewport is outside of buffer %i...",
                              operation.buffer_id);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        layer = emu_layer_new (operation.layer_id,
                               operation.width,
                               operation.height);

        if (layer == NULL)
        {
                SERVER_ERROR ("Cannot create new layer%i (%ix%i)",
                              operation.layer_id,
                              operation.width, operation.height);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        g_return_val_if_fail (layer != NULL, FALSE);

        if (emu_mixer_add_layer (mixer, layer) < 0)
        {
                emu_layer_free (layer);
                SERVER_ERROR ("Cannot add layer%i to mixer...",
                              operation.layer_id);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        emu_layer_set_viewport_input (layer,
                                      operation.src.x, operation.src.y,
                                      operation.src.w, operation.src.h);
        emu_layer_set_viewport_output (layer,
                                       operation.dst.x, operation.dst.y,
                                       operation.dst.w, operation.dst.h);

        emu_layer_set_buffer (layer, buffer);
        res_operation.result = LAZY_OPERATION_RESULT_SUCCESS;

        return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
}

static gboolean
server_input_dellayer (GIOChannel *source,
                       emu_mixer_t *mixer)
{
        lazy_operation_dellayer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);
        lazy_operation_dellayer_res_t res_operation;

        res_operation.result = LAZY_OPERATION_RESULT_FAILURE;

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot dellayer operation...");
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        SERVER_DEBUG ("del layer %i", operation.layer_id);

        emu_mixer_del_layer (mixer, operation.layer_id);
        res_operation.result = LAZY_OPERATION_RESULT_SUCCESS;

        return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
}

static gboolean
server_input_fliplayer (GIOChannel *source,
                        emu_mixer_t *mixer)
{
        lazy_operation_fliplayer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);
        lazy_operation_fliplayer_res_t res_operation;
        emu_layer_t *layer;
        emu_buffer_t *buffer;

        res_operation.result = LAZY_OPERATION_RESULT_FAILURE;

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot dellayer operation...");
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        SERVER_DEBUG ("flip layer %i", operation.layer_id);

        layer = emu_mixer_find_layer (mixer, operation.layer_id);
        if (layer == NULL)
        {
                SERVER_ERROR ("Cannot find layer %i in mixer...",
                              operation.layer_id);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        buffer = emu_buffer_pool_find_buffer (mixer->buffer_pool, operation.buffer_id);
        if (buffer == NULL)
        {
                SERVER_ERROR ("Cannot find buffer %i in mixer...",
                              operation.buffer_id);
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        SERVER_DEBUG ("Flipping to buffer %x", buffer->id);
        emu_layer_set_buffer (layer, buffer);
        res_operation.result = LAZY_OPERATION_RESULT_SUCCESS;

        return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
}

static gboolean
server_input_addbuffer (GIOChannel *source,
                        emu_mixer_t *mixer)
{
        lazy_operation_addbuffer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);
        lazy_operation_addbuffer_res_t res_operation;
        emu_buffer_t *buffer;

        res_operation.result = LAZY_OPERATION_RESULT_FAILURE;

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot addbuffer operation...");
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        SERVER_DEBUG ("add buffer %ix%i bpp=%i",
                      operation.width, operation.height, operation.bpp);

        buffer = emu_buffer_pool_add_buffer (mixer->buffer_pool,
                                             operation.width, operation.height,
                                             operation.bpp);
        if (buffer != NULL)
        {
                SERVER_DEBUG ("\tbuffer=%p file=%s", buffer, buffer->filename);

                res_operation.result = LAZY_OPERATION_RESULT_SUCCESS;
                res_operation.buffer_id = buffer->id;
        }
        else
        {
                SERVER_ERROR ("Cannot add buffer to pool...");
        }

        /* Roger that... */
        return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
}

static gboolean
server_input_delbuffer (GIOChannel *source,
                        emu_mixer_t *mixer)
{
        lazy_operation_delbuffer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);
        lazy_operation_delbuffer_res_t res_operation;

        res_operation.result = LAZY_OPERATION_RESULT_FAILURE;

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot dellayer operation...");
                return server_input_send_result (source, &res_operation,
                                                 sizeof (res_operation));
        }

        SERVER_DEBUG ("del buffer %i", operation.buffer_id);

        emu_buffer_pool_del_buffer (mixer->buffer_pool, operation.buffer_id);
        res_operation.result = LAZY_OPERATION_RESULT_SUCCESS;

        /* Roger that... */
        return server_input_send_result (source, &res_operation,
                                         sizeof (res_operation));
}

static gboolean
server_input_callback (GIOChannel *source,
                       GIOCondition condition,
                       emu_mixer_t *mixer)
{
        lazy_operation_t op;
        gsize readdata = 0;

        SERVER_DEBUG ("callback cond=%i!", condition);

        if (condition & G_IO_HUP) {
                return FALSE;
        }

        if (g_io_channel_read (source,
                                (gchar *) &op,
                                sizeof (op),
                                &readdata) != G_IO_ERROR_NONE)
        {
                SERVER_ERROR ("Cannot read operation...");
                return FALSE;
        }

        if (readdata == 0)
        {
                SERVER_DEBUG ("Closing connection...");
                return FALSE;
        }

        switch (op)
        {
        case LAZY_OPERATION_ADD_LAYER:
                return server_input_addlayer (source, mixer);
                break;

        case LAZY_OPERATION_DEL_LAYER:
                return server_input_dellayer (source, mixer);
                break;

        case LAZY_OPERATION_FLIP_LAYER:
                return server_input_fliplayer (source, mixer);
                break;

        case LAZY_OPERATION_ADD_BUFFER:
                return server_input_addbuffer (source, mixer);
                break;

        case LAZY_OPERATION_DEL_BUFFER:
                return server_input_delbuffer (source, mixer);
                break;

        default:
                SERVER_ERROR ("Unknown operation...");
                return FALSE;
        }

        return TRUE;
}

static gboolean
server_accept_callback (GIOChannel *source,
                        GIOCondition condition,
                        emu_mixer_t *mixer)

{
        int fd;
        int socket;
        GIOChannel *ioc;

        SERVER_DEBUG ("New connection...");

        fd = g_io_channel_unix_get_fd (source);
        socket = accept (fd, NULL, NULL);

        ioc = g_io_channel_unix_new (socket);
        connection_id = g_io_add_watch (ioc,
                                        G_IO_IN | G_IO_HUP,
                                        (GIOFunc) server_input_callback,
                                        mixer);

        return TRUE;
}

void
server_setup_connection (emu_mixer_t *mixer)
{
        int fd;
	ssize_t len;
        struct sockaddr_in sv_addr;
        GIOChannel *ioc;

        if (!mixer)
        {
                SERVER_ERROR ("No mixer...");
                exit (1);
        }

        if ((fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror ("socket");
                exit (1);
	}

        len = sizeof (struct sockaddr_in);

	memset (&sv_addr, 0, sizeof (struct sockaddr_in));

	sv_addr.sin_family = AF_INET;
	sv_addr.sin_port = htons (LAZY_PASSTHROUGH_PORT);
	sv_addr.sin_addr.s_addr = htonl (INADDR_ANY);

	setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (void *) &len, sizeof (len));

	if (bind (fd, (struct sockaddr *) &sv_addr, sizeof (struct sockaddr)) < 0)
	{
		perror ("bind");
		close (fd);
		exit (1);
	}

        listen (fd, 1);

        ioc = g_io_channel_unix_new (fd);

        connection_id = g_io_add_watch (ioc,
                                        G_IO_IN,
                                        (GIOFunc) server_accept_callback,
                                        mixer);
}

int
main (int argc, char *argv[])
{
        ClutterActor *stage;
        GtkWidget    *window, *clutter, *vbox;
        ClutterColor  stage_color = {
                .red = 0,
                .green = 0,
                .blue = 0xff,
                .alpha = 0xff
        };

        if (argc > 1)
                path_to_buffers = argv[1];

        if (gtk_clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
                g_error ("Unable to initialize GtkClutter");

        window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        g_signal_connect (window, "destroy",
                          G_CALLBACK (gtk_main_quit), NULL);

        vbox = gtk_vbox_new (FALSE, 6);
        gtk_container_add (GTK_CONTAINER (window), vbox);

        clutter = gtk_clutter_embed_new ();
        gtk_widget_set_size_request (clutter, WINWIDTH, WINHEIGHT);

        gtk_container_add (GTK_CONTAINER (vbox), clutter);

        stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (clutter));
        clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

        gtk_widget_show_all (window);

        emu_mixer_t *mixer = emu_mixer_new (CLUTTER_STAGE (stage));
        if (!mixer)
        {
                fprintf (stderr, "Cannot create mixer...\n");
                exit (1);
        }
        server_setup_connection (mixer);

        gtk_main();

        return 0;
}
