#include <gegl.h>
#include <glib/gprintf.h>
#include <stdint.h>

GeglNode   *gegl_decode   = NULL;
GeglNode   *gegl_display  = NULL;
GeglNode   *display       = NULL;
GeglBuffer *video_frame   = NULL;
GeglBuffer *terrain       = NULL;

#define NEGL_RGB_HEIGHT    42
#define NEGL_RGB_HIST_DIM   6 // if changing make dim * dim * dim divisible by 3
#define NEGL_FFT_DIM       64

typedef struct FrameInfo  
{
  uint8_t rgb_middle[NEGL_RGB_HEIGHT*3];
  uint8_t rgb_hist[NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM];
  uint8_t audio_energy[3];
  uint8_t audio_fft[NEGL_FFT_DIM*3];
  uint8_t scene_change_estimate[3];
} FrameInfo;

#define TERRAIN_STRIDE sizeof(FrameInfo)
#define TERRAIN_WIDTH  (TERRAIN_STRIDE/3)

int frame_start = 0;
int frame_end   = 0+ 1500;

typedef enum NeglRunMode {
  NEGL_NO_UI = 0,
  NEGL_VIDEO,
  NEGL_TERRAIN
} NeglRunmode;

#include <string.h>

//int negl_run_mode = NEGL_VIDEO;
int negl_run_mode = NEGL_TERRAIN;
//int negl_run_mode = NEGL_NO_UI;

gint
main (gint    argc,
      gchar **argv)
{
  gegl_init (&argc, &argv);
  gegl_decode = gegl_node_new ();
  gegl_display = gegl_node_new ();

  GeglNode *store = gegl_node_new_child (gegl_decode,
                                         "operation", "gegl:buffer-sink",
                                         "buffer", &video_frame, NULL);
  GeglNode *load = gegl_node_new_child (gegl_decode,
                              "operation", "gegl:ff-load",
                              "frame", 0,
                              "path", argv[1],
                              NULL);
  gegl_node_link_many (load, store, NULL);


  GeglNode *display = NULL;
  GeglNode *readbuf;

  GeglBuffer *terrain = NULL;
  GeglRectangle terrain_rect = {0, 0,
                                frame_end - frame_start + 1,
                                TERRAIN_WIDTH
};
  terrain = gegl_buffer_new (&terrain_rect, babl_format ("R'G'B' u8"));

        switch(negl_run_mode)
        {
          case NEGL_NO_UI:
            break;
          case NEGL_VIDEO:
            readbuf = gegl_node_new_child (gegl_display, "operation", "gegl:buffer-source", NULL);
            display = gegl_node_create_child (gegl_display, "gegl:display");
  	    gegl_node_link_many (readbuf, display, NULL);
            break;
          case NEGL_TERRAIN:
            readbuf = gegl_node_new_child (gegl_display, "operation", "gegl:buffer-source", NULL);
            display = gegl_node_create_child (gegl_display, "gegl:display");
	    gegl_node_set (readbuf, "buffer", terrain, NULL);
  	    gegl_node_link_many (readbuf, display, NULL);
            break;
        }

  /* a graph for looking at the video */

  {
    gint frame;
        FrameInfo info = {0};
    for (frame = frame_start; frame <= frame_end; frame++)
      {
  	int rgb_hist[NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM]={0,};
        int sum = 0;
        int second_max_hist = 0;
        int max_hist = 0;

        GeglRectangle terrain_row = {frame-frame_start, 0, 1, TERRAIN_WIDTH};

	if (video_frame)
	  g_object_unref (video_frame);
	video_frame = NULL;
        gegl_node_set (load, "frame", frame, NULL);
        gegl_node_process (store);

	GeglBufferIterator *it = gegl_buffer_iterator_new (video_frame, NULL, 0,
                babl_format ("R'G'B' u8"),
                GEGL_BUFFER_READ,
                GEGL_ABYSS_NONE);


        int slot;
        for (slot = 0; slot < NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM; slot ++)
          rgb_hist[slot] = 0;

	while (gegl_buffer_iterator_next (it))
        {
          uint8_t *data = (void*)it->data[0];
          int i;
          for (i = 0; i < it->length; i++)
          {
            int r = data[i * 3 + 0] / 256.0 * NEGL_RGB_HIST_DIM;
            int g = data[i * 3 + 1] / 256.0 * NEGL_RGB_HIST_DIM;
            int b = data[i * 3 + 2] / 256.0 * NEGL_RGB_HIST_DIM;
            int slot = r * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM +
		       g * NEGL_RGB_HIST_DIM +
		       b;
            if (slot < 0) slot = 0;
            if (slot >= NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM)
                slot = NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM;

            rgb_hist[slot]++;
            if (rgb_hist[slot] > max_hist)
            {
              second_max_hist = max_hist;
              max_hist = rgb_hist[slot];
            }
            sum++;
          }
        }

        {
           int slot;
           for (slot = 0; slot < NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM * NEGL_RGB_HIST_DIM; slot ++)
{
     	   //info.rgb_hist[slot] = rgb_hist[slot] / ((second_max_hist + max_hist) * 0.5) * 255;
           int val = rgb_hist[slot] / (sum * 1.0) * 512;
           if (val > 255) val = 255; 
     	   info.rgb_hist[slot] = val;
}
        }

        GeglRectangle mid_row;
        mid_row.x = 
           gegl_buffer_get_extent (video_frame)-> width * 1.0 * mid_row.height / gegl_buffer_get_extent (video_frame)->height / 2,
        mid_row.y = 0;
        mid_row.width = 1;
        mid_row.height = NEGL_RGB_HEIGHT;
        gegl_buffer_get (video_frame, &mid_row, 
           1.0 * mid_row.height / gegl_buffer_get_extent (video_frame)->height,
           babl_format ("R'G'B' u8"),
           (void*)&(info.rgb_middle)[0],
           GEGL_AUTO_ROWSTRIDE,
           GEGL_ABYSS_NONE);

        gegl_buffer_set (terrain, &terrain_row, 0, babl_format("RGB u8"),
                         &(info.rgb_middle[0]),
                         3);

        switch(negl_run_mode)
        {
          case NEGL_NO_UI:
            break;
          case NEGL_VIDEO:
	    gegl_node_set (readbuf, "buffer", video_frame, NULL);
	    gegl_node_process (display);
            break;
          case NEGL_TERRAIN:
	    gegl_node_set (readbuf, "buffer", terrain, NULL);
	    gegl_node_process (display);
            break;
        }
      }
  }

  if (video_frame)
    g_object_unref (video_frame);
  video_frame = NULL;
  g_object_unref (gegl_decode);
  g_object_unref (gegl_display);

  gegl_exit ();
  return 0;
}
