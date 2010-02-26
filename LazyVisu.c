#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <gtk/gtk.h>
#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>

#define HAVE_UI_DEBUG
#define HAVE_SERVER_DEBUG

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

typedef struct
{
        gint   id;
        gchar *filename;
        int    fd;

        void  *ptr;
        gint   width, height;
        gsize  size;

        GdkRectangle src;
        GdkRectangle dst;

        ClutterActor *actor;
} emu_layer_t;

typedef struct
{
        GList        *layers;
        ClutterStage *stage;
} emu_mixer_t;

/**/
gchar *path_to_buffers = DEFAULT_BUFFER_PATH;

/**/
void
emu_layer_free (emu_layer_t *layer)
{
        if (!layer)
                return;

        if (layer->actor)
        {
                layer->actor = NULL;
        }

        if (layer->ptr)
        {
                munmap (layer->ptr, layer->size);
                layer->ptr = NULL;
        }

        if (layer->fd != -1)
        {
                close (layer->fd);
                layer->fd = -1;
        }

        if (layer->filename)
        {
                g_free (layer->filename);
                layer->filename = NULL;
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
        layer->size = 4 * width * height;

        layer->fd = -1;

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
emu_layer_set_buffer (emu_layer_t *layer, const gchar *filename)
{
        gboolean  buffer_changed = FALSE;
        gchar    *tmp_filename;
        int       fd = -1;
        void     *ptr = NULL;

        g_return_if_fail ((layer != NULL) || (filename != NULL));

        tmp_filename = g_strdup_printf ("%s/%s",
                                        path_to_buffers,
                                        filename);
        g_return_if_fail (tmp_filename != NULL);

        UI_DEBUG ("buffer in %s", tmp_filename);

        if ((layer->filename == NULL) ||
            strcmp (tmp_filename, layer->filename))
        {
                buffer_changed = TRUE;

                if ((fd = open (tmp_filename, O_RDONLY)) == -1)
                {
                        g_error ("Cannot open file %s : %s...",
                                 tmp_filename, strerror (errno));
                        g_free (tmp_filename);
                        return;
                }
                UI_DEBUG ("buffer size %lu...", layer->size);

                if ((ptr = mmap (NULL, layer->size,
                                 PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED)
                {
                        g_error ("Cannot mmap file %s : %s...",
                                 tmp_filename, strerror (errno));
                        close (fd);
                        g_free (tmp_filename);
                        return;
                }
                UI_DEBUG ("buffer @ %p...", ptr);
        }

        clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (layer->actor),
                                           (ptr != NULL) ? ptr : layer->ptr,
                                           TRUE,
                                           layer->width,
                                           layer->height,
                                           4 * layer->width,
                                           4,
                                           CLUTTER_TEXTURE_RGB_FLAG_BGR,
                                           NULL);
        clutter_actor_queue_redraw (layer->actor);

        if (buffer_changed)
        {
                if (layer->ptr)
                        munmap (layer->ptr, layer->size);
                layer->ptr = ptr;

                if (layer->fd != -1)
                        close (layer->fd);
                layer->fd = fd;

                if (layer->filename)
                        g_free (layer->filename);
                layer->filename = tmp_filename;
        }
}

/**/
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

        return mixer;
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

/**/
#define __LONG_TYPE_64

#include "lazy_passthrough_internal.h"

guint connection_id = 0;

static gboolean
server_input_addlayer (GIOChannel *source,
                       emu_mixer_t *mixer)
{
        lazy_int_t ret;
        lazy_operation_addlayer_t operation;
        emu_layer_t *layer;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot addlayer operation...");
                return FALSE;
        }

        SERVER_DEBUG ("add layer %ix%i@%ix%i -> %ix%i@%ix%i",
                      operation.src.w, operation.src.h,
                      operation.src.x, operation.src.y,
                      operation.dst.w, operation.dst.h,
                      operation.dst.x, operation.dst.y);

        layer = emu_layer_new (operation.layer_num,
                               operation.width,
                               operation.height);

        g_return_val_if_fail (layer != NULL, FALSE);

        if (emu_mixer_add_layer (mixer, layer) < 0)
        {
                emu_layer_free (layer);
                SERVER_ERROR ("Cannot add layer to mixer...");

                return FALSE;
        }

        emu_layer_set_viewport_input (layer,
                                      operation.src.x, operation.src.y,
                                      operation.src.w, operation.src.h);
        emu_layer_set_viewport_output (layer,
                                       operation.dst.x, operation.dst.y,
                                       operation.dst.w, operation.dst.h);
        emu_layer_set_buffer (layer, operation.filename);

        /* Roger that... */
        ret = 1;
        if ((g_io_channel_write (source,
                                (gchar *) &ret, sizeof (ret),
                                 &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != sizeof (ret)))
        {
                SERVER_ERROR ("Cannot ack addlayer operation...");
                return FALSE;
        }

        return TRUE;
}

static gboolean
server_input_dellayer (GIOChannel *source,
                       emu_mixer_t *mixer)
{
        lazy_int_t ret;
        lazy_operation_dellayer_t operation;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot dellayer operation...");
                return FALSE;
        }

        SERVER_DEBUG ("del layer %i", operation.layer_num);

        emu_mixer_del_layer (mixer, operation.layer_num);

        /* Roger that... */
        ret = 1;
        if ((g_io_channel_write (source,
                                 (gchar *) &ret, sizeof (ret),
                                 &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != sizeof (ret)))
        {
                SERVER_ERROR ("Cannot ack addlayer operation...");
                return FALSE;
        }

        return TRUE;
}

static gboolean
server_input_fliplayer (GIOChannel *source,
                        emu_mixer_t *mixer)
{
        lazy_int_t ret;
        lazy_operation_fliplayer_t operation;
        emu_layer_t *layer;
        gsize transfereddata = 0;
        const gsize toreaddata = sizeof (operation) - sizeof (lazy_operation_t);

        if ((g_io_channel_read (source,
                                ((gchar *) &operation) + sizeof (lazy_operation_t),
                                toreaddata,
                                &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != toreaddata))
        {
                SERVER_ERROR ("Cannot dellayer operation...");
                return FALSE;
        }

        SERVER_DEBUG ("flip layer %i", operation.layer_num);

        layer = emu_mixer_find_layer (mixer, operation.layer_num);
        if (layer == NULL)
        {
                SERVER_ERROR ("Cannot find layer %i in mixer...",
                              operation.layer_num);
                return FALSE;
        }

        emu_layer_set_buffer (layer, operation.filename);

        /* Roger that... */
        ret = 1;
        if ((g_io_channel_write (source,
                                 (gchar *) &ret, sizeof (ret),
                                 &transfereddata) != G_IO_ERROR_NONE) ||
            (transfereddata != sizeof (ret)))
        {
                SERVER_ERROR ("Cannot ack fliplayer operation...");
                return FALSE;
        }

        return TRUE;
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
        case LAZY_ADD_LAYER:
                return server_input_addlayer (source, mixer);
                break;

        case LAZY_DEL_LAYER:
                return server_input_dellayer (source, mixer);
                break;

        case LAZY_FLIP_LAYER:
                return server_input_fliplayer (source, mixer);
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
