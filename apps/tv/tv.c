/*
 *  Generic browser
 *  Copyright (C) 2007 Andreas �man
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define _GNU_SOURCE

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "layout/layout.h"
#include "event.h"
#include "htsp.h"

#if 0

static void tv_unsubscribe(tv_t *tv, tv_channel_t *ch);

/**
 *
 */
static void
tv_show_channel_menu(tv_t *tv)
{
  glw_t *w;
  glw_prop_set_int(tv->tv_prop_show_channel_menu, 1);

  if((w = glw_find_by_id(tv->tv_rootwidget, "menuFocus", 0)) != NULL) 
    glw_select(w);
}


/**
 * Called from GLW when user selecs a different audio track
 */
static void
tv_set_audio_track(void *opaque, void *opaque2, int value)
{
  tv_channel_t *ch = opaque;

  ch->ch_mp->mp_audio.mq_stream = value;
}


/**
 * Called from GLW when user switches detailed display
 */
static void
tv_set_display_details(void *opaque, void *opaque2, int value)
{
  tv_channel_t *ch = opaque;
  glw_prop_set_int(ch->ch_prop_show_details, value);
}

/**
 * Open menu
 */
static void
tv_subscription_open_menu(tv_t *tv, tv_channel_t *ch)
{
  glw_t *p;
  media_pipe_t *mp = ch->ch_mp;
  tv_channel_stream_t *tcs;
  
  if(ch->ch_menu != NULL) {
    glw_detach(ch->ch_menu);
    ch->ch_menu = NULL;
    return;
  }

  p = glw_find_by_id(ch->ch_subscribe_widget, "video_container", 0);
  if(p == NULL)
    return;

  ch->ch_menu =
    glw_model_create("theme://tv/subscription-menu.model", p,
		     0, 
		     ch->ch_prop_root,
		     ch->ch_mp->mp_prop_root,
		     tv->tv_prop_root,
		     prop_global,
		     NULL);
  
  /**
   * Populate audio tracks
   */
  if((p = glw_find_by_id(ch->ch_menu, "audio_tracks", 0)) != NULL) {

    LIST_FOREACH(tcs, &ch->ch_streams, tcs_link) {
      if(tcs->tcs_cw->codec->type != CODEC_TYPE_AUDIO)
	continue;

      glw_selection_add_text_option(p, tcs->tcs_title,
				    tv_set_audio_track,
				    ch, NULL, tcs->tcs_index,
				    tcs->tcs_index == mp->mp_audio.mq_stream);
    }
    glw_selection_add_text_option(p, "Off", tv_set_audio_track,
				  ch, NULL, -1, -1 == mp->mp_audio.mq_stream);
  }


  /**
   * Populate video control widgets
   */
  video_menu_attach(ch->ch_menu, &ch->ch_vdc);


  if((p = glw_find_by_id(ch->ch_menu, "details", 0)) != NULL) {
    glw_selection_add_text_option(p, "On", tv_set_display_details, ch, NULL, 1,
				  glw_prop_get_int(ch->ch_prop_show_details));

    glw_selection_add_text_option(p, "Off", tv_set_display_details,
				  ch, NULL, 0,
				  !glw_prop_get_int(ch->ch_prop_show_details));
  }

  glw_select(ch->ch_menu);

}


/**
 *  Return 0 if user wants to exit, otherwise -1
 */
static int
tv_access_error(tv_t *tv, const char *dir, char *errtxt)
{
  int r;
  glw_t *m;
  glw_prop_t *p;
  
  p = glw_prop_create(NULL, "error");
  glw_prop_set_stringf(glw_prop_create(p, "details"),
		       "\"%s\"\n%s", dir, errtxt);

  m = glw_model_create("theme://tv/access-error.model", tv->tv_stack,
		       0, p, NULL);
  r = glw_wait_form_ok_cancel(m);
  glw_detach(m);
  glw_prop_destroy(p);
  return r;
}


/**
 *
 */
tv_tag_t *
tv_tag_find(tv_t *tv, const char *identifier, int create)
{
  tv_tag_t *tt;

  TAILQ_FOREACH(tt, &tv->tv_tags, tt_tv_link)
    if(!strcmp(tt->tt_identifier, identifier))
      return tt;

  if(!create)
    return NULL;

  tt = calloc(1, sizeof(tv_tag_t));
  tt->tt_identifier = strdup(identifier);
  TAILQ_INSERT_TAIL(&tv->tv_tags, tt, tt_tv_link);
  TAILQ_INIT(&tt->tt_ctms);

  tt->tt_prop_root  = glw_prop_create(NULL, "tag");
  tt->tt_prop_title = glw_prop_create(tt->tt_prop_root, "title");
  tt->tt_prop_icon  = glw_prop_create(tt->tt_prop_root, "icon");

  tt->tt_prop_titled_icon = glw_prop_create(tt->tt_prop_root, "titledIcon");
  tt->tt_prop_nchannels = glw_prop_create(tt->tt_prop_root, "channels");

  /**
   * Create widget
   */
  tt->tt_widget = glw_model_create("theme://tv/tag.model", NULL, 0,
				   prop_global,
				   tv->tv_prop_root,
				   tt->tt_prop_root,
				   NULL);

  tt->tt_tab = glw_model_create("theme://tv/chlist.model", NULL, 0,
				   prop_global,
				   tv->tv_prop_root,
				   tt->tt_prop_root,
				   NULL);

  tt->tt_chlist = glw_find_by_id(tt->tt_tab, "channel_container", 0);

  glw_add_tab(tv->tv_rootwidget,
	      "tag_container", tt->tt_widget,
	      "chlist_container", tt->tt_tab);

  return tt;
}

/**
 *
 */
void
tv_tag_set_title(tv_tag_t *tt, const char *title)
{
  if(title != NULL)
    glw_prop_set_string(tt->tt_prop_title, title);
}


/**
 *
 */
void
tv_tag_set_icon(tv_tag_t *tt, const char *icon)
{
  if(icon != NULL)
    glw_prop_set_string(tt->tt_prop_icon, icon);
}


/**
 *
 */
void
tv_tag_set_titled_icon(tv_tag_t *tt, int v)
{
  glw_prop_set_int(tt->tt_prop_titled_icon, v);
}


/**
 *
 */
tv_channel_t *
tv_channel_find(tv_t *tv, uint32_t id, int create)
{
  tv_channel_t *ch;
  int i;
  glw_prop_t *p, *p_epg, *p_sub;
  char buf[10];

  TAILQ_FOREACH(ch, &tv->tv_channels, ch_tv_link)
    if(ch->ch_id == id)
      return ch;

  if(!create)
    return NULL;

  ch = calloc(1, sizeof(tv_channel_t));
  avgstat_init(&ch->ch_avg_bitrate, 10);
  TAILQ_INIT(&ch->ch_ctms);
  TAILQ_INSERT_TAIL(&tv->tv_channels, ch, ch_tv_link);
  ch->ch_tv = tv;
  ch->ch_id = id;

  ch->ch_prop_root  = glw_prop_create(NULL, "channel");
  ch->ch_prop_title = glw_prop_create(ch->ch_prop_root, "title");
  ch->ch_prop_icon  = glw_prop_create(ch->ch_prop_root, "icon");
  ch->ch_prop_fullscreen = glw_prop_create(ch->ch_prop_root, "fullscreen");

  ch->ch_prop_show_details = glw_prop_create(ch->ch_prop_root, "showDetails");
  
  /* EPG props */

  p_epg = glw_prop_create(ch->ch_prop_root, "epg");

  for(i = 0; i < 3; i++) {
    snprintf(buf, sizeof(buf), "event%d", i);
    p = glw_prop_create(p_epg, buf);
    
    ch->ch_prop_epg_title[i] = glw_prop_create(p, "title");
    ch->ch_prop_epg_start[i] = glw_prop_create(p, "start");
    ch->ch_prop_epg_stop[i]  = glw_prop_create(p, "stop");
  }


  /* Subscription props */

  p_sub = glw_prop_create(ch->ch_prop_root, "subscription");

  ch->ch_prop_sub_status = glw_prop_create(p_sub, "status");
  ch->ch_prop_sub_bitrate = glw_prop_create(p_sub, "bitrate");
  ch->ch_prop_sub_backend_queuesize =
    glw_prop_create(p_sub, "queuesize");
  ch->ch_prop_sub_backend_queuedelay =
    glw_prop_create(p_sub, "queuedelay");
  ch->ch_prop_sub_backend_queuedrops =
    glw_prop_create(p_sub, "queuedrops");

  return ch;
}


/**
 *
 */
void
tv_channel_set_title(tv_channel_t *ch, const char *title)
{
  if(title != NULL)
    glw_prop_set_string(ch->ch_prop_title, title);
}


/**
 *
 */
void
tv_channel_set_icon(tv_channel_t *ch, const char *icon)
{
  if(icon != NULL)
    glw_prop_set_string(ch->ch_prop_icon, icon);
}

/**
 *
 */
static void
tv_control_signal(tv_t *tv, int cmd, int key)
{
  tv_ctrl_event_t *tce;

  tce = glw_event_create(EVENT_TV, sizeof(tv_ctrl_event_t));
  tce->cmd = cmd;
  tce->key = key;
  abort(); //glw_event_enqueue(&tv->tv_ai->ai_geq, &tce->h);
}


/**
 * Callback for intercepting user events on a channel (enter, select, etc)
 */
static int
channel_widget_callback(glw_t *w, void *opaque, glw_signal_t signal,
			void *extra)
{
  glw_event_t *ge = extra;
  tv_t *tv = opaque;
  if(signal != GLW_SIGNAL_EVENT)
    return 0;

  switch(ge->ge_type) {
  default:
    break;

  case EVENT_KEY_SELECT:
    //    tv_control_signal(tv, TV_CTRL_START, w->glw_u32);
    return 1;

  case GEV_ENTER:
  case EVENT_KEY_PLAY:
    tv_control_signal(tv, TV_CTRL_CLEAR_AND_START, w->glw_u32);
    return 1;

  case EVENT_KEY_EJECT:
    //    tv_control_signal(tv, TV_CTRL_CLEAR, w->glw_u32);
    return 1;
  }
  return 0;
}


/**
 *
 */
void
tv_tag_map_channel(tv_t *tv, tv_tag_t *tt, tv_channel_t *ch)
{
  tv_channel_tag_map_t *ctm;

  TAILQ_FOREACH(ctm, &tt->tt_ctms, ctm_tag_link)
    if(ctm->ctm_channel == ch)
      break;
  if(ctm != NULL) {
    /* Maping already exist, just make sure it's not deleted */
    ctm->ctm_delete_me = 0;
    return;
  }

  ctm = calloc(1, sizeof(tv_channel_tag_map_t));
  ctm->ctm_tag = tt;
  ctm->ctm_channel = ch;
  TAILQ_INSERT_TAIL(&ch->ch_ctms, ctm, ctm_channel_link);
  TAILQ_INSERT_TAIL(&tt->tt_ctms, ctm, ctm_tag_link);

  glw_prop_set_int(tt->tt_prop_nchannels, ++tt->tt_nctms);

  /**
   * Create channel widget in tag list
   */
  ctm->ctm_widget = glw_model_create("theme://tv/channel.model",
				     tt->tt_chlist, GLW_MODEL_CACHE,
				     ch->ch_prop_root,
				     tt->tt_prop_root,
				     prop_global,
				     NULL);

  ctm->ctm_widget->glw_u32 = ch->ch_id; /* A bit ugly */

  glw_set(ctm->ctm_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, channel_widget_callback, tv, 400,
	  NULL);

  
}


/**
 *
 */
static void
ctm_destroy(tv_channel_tag_map_t *ctm)
{
  tv_channel_t *ch = ctm->ctm_channel;
  tv_tag_t *tt = ctm->ctm_tag;

  glw_destroy(ctm->ctm_widget);
  TAILQ_REMOVE(&ch->ch_ctms, ctm, ctm_channel_link);
  TAILQ_REMOVE(&tt->tt_ctms, ctm, ctm_tag_link);

  glw_prop_set_int(tt->tt_prop_nchannels, --tt->tt_nctms);

  free(ctm);
}
/**
 *
 */
void
tv_tag_mark_ctms(tv_tag_t *tt)
{
  tv_channel_tag_map_t *ctm;
  TAILQ_FOREACH(ctm, &tt->tt_ctms, ctm_tag_link)
    ctm->ctm_delete_me = 1;
}



/**
 *
 */
void
tv_tag_delete_marked_ctms(tv_tag_t *tt)
{
  tv_channel_tag_map_t *ctm, *next;

  for(ctm = TAILQ_FIRST(&tt->tt_ctms); ctm != NULL; ctm = next) {
    next = TAILQ_NEXT(ctm, ctm_tag_link);
    if(ctm->ctm_delete_me)
      ctm_destroy(ctm);
  }
}




/**
 *
 */
void
tv_channel_set_current_event(tv_channel_t *ch, int index, 
			     const char *title, time_t start, time_t stop)
{
  glw_prop_set_string(ch->ch_prop_epg_title[index], title);
  glw_prop_set_int(ch->ch_prop_epg_start[index],    start);
  glw_prop_set_int(ch->ch_prop_epg_stop[index],     stop);
}

/**
 *
 */
void
tv_channel_destroy(tv_t *tv, tv_channel_t *ch)
{
  tv_channel_tag_map_t *ctm;

  tv_unsubscribe(tv, ch);

  while((ctm = TAILQ_FIRST(&ch->ch_ctms)) != NULL)
    ctm_destroy(ctm);

  TAILQ_REMOVE(&tv->tv_channels, ch, ch_tv_link);

  glw_prop_destroy(ch->ch_prop_root);

  free(ch);
}



/**
 *
 */
void
tv_tag_destroy(tv_t *tv, tv_tag_t *tt)
{
  tv_channel_tag_map_t *ctm;

  while((ctm = TAILQ_FIRST(&tt->tt_ctms)) != NULL)
    ctm_destroy(ctm);

  TAILQ_REMOVE(&tv->tv_tags, tt, tt_tv_link);

  glw_destroy(tt->tt_widget);
  glw_destroy(tt->tt_tab);

  glw_prop_destroy(tt->tt_prop_root);

  free(tt);
}



/**
 *
 */
void
tv_remove_all(tv_t *tv)
{
  tv_channel_t *ch;
  tv_tag_t *tt;

  while((ch = TAILQ_FIRST(&tv->tv_channels)) != NULL)
    tv_channel_destroy(tv, ch);

  while((tt = TAILQ_FIRST(&tv->tv_tags)) != NULL)
    tv_tag_destroy(tv, tt);

}

/**
 * Switch the given channel to be presented in fullscreen mode
 */
static void
tv_fullscreen(tv_t *tv, tv_channel_t *ch)
{
  glw_t *w, *p;
  tv_channel_t *o;

  if(ch->ch_subscribed == 0)
    return; /* Should normally not happen, but we have a small
	       timeslot condition between the event enqueue in glw
	       callbacks and the main thread, so we just leave it */

  glw_lock();

  /* Pull out any currently fullscreen channels */
  p = tv->tv_fullscreen_container;
  if(p != NULL && (w = TAILQ_FIRST(&p->glw_childs)) != NULL) {
    /* No longer in fullscreen mode */
    glw_set(w, GLW_ATTRIB_PARENT, tv->tv_subscription_container, NULL);

    o = tv_channel_find(tv, w->glw_u32, 0);
    assert(o != NULL);

    glw_prop_set_int(o->ch_prop_fullscreen, 0);
  }

  /* Insert this channels widget into fullscreen container */
  glw_set(ch->ch_subscribe_widget,
	  GLW_ATTRIB_PARENT, tv->tv_fullscreen_container,
	  NULL);

  glw_unlock();

  tv->tv_fullscreen_channel = ch;

  glw_prop_set_int(ch->ch_prop_fullscreen, 1);

  //  tv->tv_ai->ai_req_fullscreen = 1;

  glw_prop_set_int(tv->tv_prop_show_channel_menu, 0);
}






/**
 * Stop playback on a channel.
 * Internal code only, this does not communicate with backend.
 *
 * 'tv_ch_mutex' must be locked.
 */
void
tv_channel_stop(tv_t *tv, tv_channel_t *ch)
{
  formatwrap_t *fw = ch->ch_fw;
  media_pipe_t *mp = ch->ch_mp;
  tv_channel_stream_t *tcs;

  if(ch->ch_running == 0)
    return;

  if(tv->tv_fullscreen_channel == ch)
    tv->tv_fullscreen_channel = NULL;

#if 0
  if(glw_prop_get_int(ch->ch_prop_fullscreen))
    tv->tv_ai->ai_req_fullscreen = 0;
#endif

  ch->ch_running = 0;

  /* Remove from running link, no more packets should enter the pipeline */
  pthread_mutex_lock(&tv->tv_running_mutex);
  LIST_REMOVE(ch, ch_running_link);
  pthread_mutex_unlock(&tv->tv_running_mutex);

  /* Drain packet queues */
  mp_flush(mp);

  /* Close codecs in the correct way */

  while((tcs = LIST_FIRST(&ch->ch_streams)) != NULL) {
    wrap_codec_deref(tcs->tcs_cw);
    LIST_REMOVE(tcs, tcs_link);
    free(tcs->tcs_title);
    free(tcs);
  }

  wrap_format_destroy(fw);

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  ch->ch_fw = NULL;
}


#if 0
/**
 * Callback for intercepting user events on a subscription
 */
static int
subscription_widget_callback(glw_t *w, void *opaque, glw_signal_t signal,
			     void *extra)
{
  glw_event_t *ge = extra;
  tv_t *tv = opaque;
  if(signal != GLW_SIGNAL_EVENT)
    return 0;

  switch(ge->ge_type) {
  default:
    break;

  case GEV_ENTER:
    tv_control_signal(tv, TV_CTRL_FULLSCREEN, w->glw_u32);
    return 1;
#if 0
  case EVENT_KEY_STOP:
    tv_control_signal(tv, TV_CTRL_STOP, w->glw_u32);
    return 1;
#endif
  }
  return 0;
}
#endif

/**
 * Subscribe to a channel.
 * - Send subscription request to backend.
 * - Create widgets, and media pipe
 *
 * 'fs' is used to start the subscription in fullscreen mode
 * 
 * 'tv_ch_mutex' must be locked.
 */
static void
tv_subscribe(tv_t *tv, tv_channel_t *ch, int fs)
{
  glw_t *vwp;
  char errbuf[100];

  if(tv->tv_be_subscribe == NULL)
    return; /* No backend, can't do anything */

  if(ch->ch_subscribed)
    return; /* Subscribe twice does not do anything.
	       Perhaps we should pop the subscription to front? */

  ch->ch_subscribed = 1;

  ch->ch_mp = mp_create("TV");

  ch->ch_subscribe_widget =
    glw_model_create("theme://tv/subscription.model",
		     fs ? NULL : tv->tv_subscription_container,
		     GLW_MODEL_CACHE,
		     ch->ch_prop_root,
		     ch->ch_mp->mp_prop_root,
		     tv->tv_prop_root,
		     prop_global,
		     NULL);

  ch->ch_subscribe_widget->glw_u32 = ch->ch_id; /* A bit ugly */

#if 0
  glw_set(ch->ch_subscribe_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, subscription_widget_callback, tv, 400,
	  NULL);
#endif

  if(fs)
    tv_fullscreen(tv, ch);
  else
    glw_prop_set_int(ch->ch_prop_fullscreen, 0);


  vwp = glw_find_by_id(ch->ch_subscribe_widget, "video_container", 0);


  vd_conf_init(&ch->ch_vdc);

  ch->ch_video_widget = vd_create_widget(vwp, ch->ch_mp, 0.0);

  mp_set_video_conf(ch->ch_mp, &ch->ch_vdc);

  ch->ch_playstatus_start_flags = fs ? 0 : MP_DONT_GRAB_AUDIO;

  if(tv->tv_be_subscribe(tv->tv_be_opaque, ch, errbuf, sizeof(errbuf)) < 0)
    glw_prop_set_string(ch->ch_prop_sub_status, errbuf);
}

/**
 * Unsubscribe from a channel.
 * - Destroy widgets, and media pipe
 * - Send subscription stop to backend.
 *
 * We don't actually wait for response from the backend.
 * Instead we just tear down everything.
 *
 * 'tv_ch_mutex' must be locked.
 */
static void
tv_unsubscribe(tv_t *tv, tv_channel_t *ch)
{
  if(!ch->ch_subscribed)
    return; /* Already unsubscribed */

  /* If backend no longer has an unsubscribe callback we assume
     that it has cleaned up everything itself, thus we just skip
     over the call */

  if(tv->tv_be_unsubscribe != NULL)
    tv->tv_be_unsubscribe(tv->tv_be_opaque, ch);

  tv_channel_stop(tv, ch);

  /* Destroy subscription widget (video, etc) */
  glw_destroy(ch->ch_subscribe_widget);

  /* If there was a menu around it would also have been destroyed */
  ch->ch_menu = NULL;


  /* Unref media_pipe */
  mp_unref(ch->ch_mp);
  ch->ch_mp = NULL;
  ch->ch_subscribed = 0;
}


/**
 *
 */
static void
tv_unsubscribe_all(tv_t *tv)
{
  tv_channel_t *ch;

  TAILQ_FOREACH(ch, &tv->tv_channels, ch_tv_link)
    tv_unsubscribe(tv, ch);
}





/**
 *
 */
static void
handle_tv_ctrl_event(tv_t *tv, tv_ctrl_event_t *tce)
{
  tv_channel_t *ch;

  switch(tce->cmd) {
  case TV_CTRL_CLEAR_AND_START:
    tv_unsubscribe_all(tv);
    if((ch = tv_channel_find(tv, tce->key, 0)) == NULL)
      break;
    tv_subscribe(tv, ch, 1);
    break;

  case TV_CTRL_START:
    if((ch = tv_channel_find(tv, tce->key, 0)) == NULL)
      break;
    tv_subscribe(tv, ch, 0);
    break;

  case TV_CTRL_CLEAR:
    tv_unsubscribe_all(tv);
    break;

  case TV_CTRL_STOP:
    if((ch = tv_channel_find(tv, tce->key, 0)) == NULL)
      break;
    tv_unsubscribe(tv, ch);
    break;

  case TV_CTRL_FULLSCREEN:
    if((ch = tv_channel_find(tv, tce->key, 0)) == NULL)
      break;
    tv_fullscreen(tv, ch);
    break;
  }
}



/**
 * Store information about this TV instance on disk
 */
#if 0
static void
tv_store_instance(appi_t *ai, tv_t *tv)
{
  htsmsg_t *m = appi_settings_create(ai);
  htsmsg_add_str(m, "url", glw_prop_get_string(tv->tv_prop_url));
  appi_settings_save(ai, m);
}
#endif

#if 0

/**
 * TV configuration
 */
static void
tv_config(tv_t *tv, appi_t *ai)
{
  glw_t *m;
  glw_event_t *ge;
  glw_event_appmethod_t *gea;

  m = glw_model_create("theme://tv/config.model", tv->tv_stack, 0,
		       prop_global, 
		       tv->tv_prop_root, 
		       ai->ai_prop_root, 
		       NULL);

  appi_speedbutton_mapper(m, "speedbutton", ai);

  ge = glw_wait_form(m);

  if(ge->ge_type == GEV_OK) {
    glw_prop_set_from_widget(m, "title",    ai->ai_prop_title);
    glw_prop_set_from_widget(m, "url",      tv->tv_prop_url);

    tv_store_instance(ai, tv);

    tv->tv_runstatus = TV_RS_RECONFIGURE;
  } else if(ge->ge_type == GEV_CANCEL) {
    tv->tv_runstatus = TV_RS_RUN;

  } else if(ge->ge_type == GEV_APPMETHOD) {
    gea = (glw_event_appmethod_t *)ge;

    if(!strcmp(gea->method, "appQuit"))
      tv->tv_runstatus = TV_RS_STOP;
  }

  glw_event_unref(ge);
  glw_detach(m);
}
#endif


/**
 *
 */
static int
tv_main(tv_t *tv)
{
  glw_event_t *ge;
  const char *url;
  htsp_connection_t *hc;

  //  glw_event_flushqueue(&ai->ai_geq);

  while(tv->tv_runstatus != TV_RS_STOP) {

    /**
     * Create browser root
     */ 
    url = glw_prop_get_string(tv->tv_prop_url);
    
    if((hc = htsp_create(url, tv)) == NULL) {
      tv_access_error(tv, ai, url, "Can not connect");
      tv_config(tv, ai);
    } else {
      tv->tv_runstatus = TV_RS_RUN;
    }

    while(tv->tv_runstatus == TV_RS_RUN) {

      ge = glw_event_get(-1, &ai->ai_geq);

      switch(ge->ge_type) {
      default:
	break;

      case EVENT_RECONFIGURE:
	htsp_destroy(hc);
	hc = NULL;
	tv_config(tv, ai);
	break;

      case EVENT_KEY_MENU:
	if(tv->tv_fullscreen_channel == NULL) {
	  tv_config(tv, ai);
	  break;
	}
	tv_subscription_open_menu(tv, tv->tv_fullscreen_channel);
	break;

      case GEV_BACKSPACE:
	if(glw_prop_get_int(tv->tv_prop_show_channel_menu))
	  mainmenu_show(ai);
	else
	  tv_show_channel_menu(tv);
	break;

      case EVENT_KEY_STOP:
	hts_mutex_lock(&tv->tv_ch_mutex);

	if(tv->tv_fullscreen_channel != NULL)
	  tv_unsubscribe(tv, tv->tv_fullscreen_channel);

	hts_mutex_unlock(&tv->tv_ch_mutex);
	tv_show_channel_menu(tv);
	break;

      case EVENT_TV:
	hts_mutex_lock(&tv->tv_ch_mutex);
	handle_tv_ctrl_event(tv, (tv_ctrl_event_t *)ge);
	hts_mutex_unlock(&tv->tv_ch_mutex);
	break;
      }
      glw_event_unref(ge);
    }

    if(hc != NULL)
      htsp_destroy(hc);
    hc = NULL;
  }

  return 0;
}


/**
 * Launch a TV
 *
 * If aux (settings) is non-NULL, we read settings from it, otherwise
 * ask user for settings
 */
static void *
tv_launch(void *aux)
{
  tv_t *tv = alloca(sizeof(tv_t));
  appi_t *ai = aux;
  const char *s;
  glw_prop_t *p_be;

  memset(tv, 0, sizeof(tv_t));

  TAILQ_INIT(&tv->tv_tags);
  TAILQ_INIT(&tv->tv_channels);

  tv->tv_ai = ai;


  tv->tv_prop_root = glw_prop_create(NULL, "tv");
  tv->tv_prop_url  = glw_prop_create(tv->tv_prop_root, "url");

  tv->tv_prop_show_channel_menu = glw_prop_create(tv->tv_prop_root, 
						  "showChannelMenu");
 
  glw_prop_set_int(tv->tv_prop_show_channel_menu, 1);

  p_be = glw_prop_create(tv->tv_prop_root, "backend");

  tv->tv_prop_backend_error = glw_prop_create(p_be, "error");
  tv->tv_prop_backend_name  = glw_prop_create(p_be, "name");

  tv->tv_stack = ai->ai_widget = 
    glw_create(GLW_ZSTACK,
	       NULL);

  tv->tv_rootwidget =
    glw_model_create("theme://tv/tv-app.model", ai->ai_widget,
		     0,  
		     tv->tv_prop_root, 
		     ai->ai_prop_root,
		     prop_global, 
		     NULL);


  tv->tv_subscription_container =
    glw_find_by_id(tv->tv_rootwidget, "subscription_container", 0);
  
  tv->tv_fullscreen_container = 
    glw_find_by_id(tv->tv_rootwidget, "fullscreen_container", 0);


  glw_set(tv->tv_rootwidget,
	  GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &ai->ai_geq, 1000,
	  NULL);

  /**
   *  Switcher miniature
   */

  ai->ai_miniature =
    glw_model_create("theme://tv/tv-miniature.model", NULL,  0, 
		     tv->tv_prop_root, 
		     ai->ai_prop_root,
		     prop_global, 
		     NULL);

  mainmenu_appi_add(ai, 1);
  


  /**
   * load configuration (if present)
   */
  if(ai->ai_settings != NULL) {
    if((s = htsmsg_get_str(ai->ai_settings, "url")) != NULL)
      glw_prop_set_string(tv->tv_prop_url, s);

  } else {

    glw_prop_set_string(tv->tv_prop_url, "htsp://127.0.0.1:9982");

    layout_appi_show(ai);
    tv_config(tv, ai);
  }

  tv_main(tv, ai);

  glw_destroy(ai->ai_miniature);
  glw_destroy(ai->ai_widget);

  appi_destroy(ai);
  glw_prop_destroy(tv->tv_prop_root);
  mainmenu_show(NULL);
  return NULL;
}



/**
 * Start a new tv thread
 */
static void
tv_spawn(appi_t *ai)
{
  hts_thread_t tid;
  hts_thread_create_detached(&tid, tv_launch, ai);
}


app_t app_tv = {
  .app_spawn = tv_spawn,
  .app_name = "TV",
  .app_model = "theme://tv/start-icon.model",
};
#endif
