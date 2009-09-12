/* evilwm - Minimalist Window Manager for X
 * Copyright (C) 1999-2009 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include "evilwm.h"
#include "log.h"

static int interruptibleXNextEvent(XEvent *event);

#ifdef DEBUG
const char *debug_atom_name(Atom a);
const char *debug_atom_name(Atom a) {
	static char buf[48];
	char *atom_name = XGetAtomName(dpy, a);
	strncpy(buf, atom_name, sizeof(buf));
	buf[sizeof(buf)-1] = 0;
	return buf;
}
#endif

static void current_to_head(void) {
	Client *c;
	if (current && current != head_client) {
		for (c = head_client; c; c = c->next) {
			if (c->next == current) {
				c->next = current->next;
				current->next = head_client;
				head_client = current;
				break;
			}
		}
	}
}

static void handle_key_event(XKeyEvent *e) {
	KeySym key = XKeycodeToKeysym(dpy,e->keycode,0);
	Client *c;
	int width_inc, height_inc;
#ifdef VWM
	ScreenInfo *current_screen = find_current_screen();
#endif

	switch(key) {
		case KEY_NEW:
			spawn(opt_term);
			break;
		case KEY_NEXT:
			next();
			if (XGrabKeyboard(dpy, e->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
				XEvent ev;
				do {
					XMaskEvent(dpy, KeyPressMask|KeyReleaseMask, &ev);
					if (ev.type == KeyPress && XKeycodeToKeysym(dpy,ev.xkey.keycode,0) == KEY_NEXT)
						next();
				} while (ev.type == KeyPress || XKeycodeToKeysym(dpy,ev.xkey.keycode,0) == KEY_NEXT);
				XUngrabKeyboard(dpy, CurrentTime);
			}
			current_to_head();
			break;
#ifdef VWM
		case XK_1: case XK_2: case XK_3: case XK_4:
		case XK_5: case XK_6: case XK_7: case XK_8:
			switch_vdesk(current_screen, KEY_TO_VDESK(key));
			break;
		case KEY_PREVDESK:
			if (current_screen->vdesk > KEY_TO_VDESK(XK_1)) {
				switch_vdesk(current_screen,
						current_screen->vdesk - 1);
			}
			break;
		case KEY_NEXTDESK:
			if (current_screen->vdesk < KEY_TO_VDESK(XK_8)) {
				switch_vdesk(current_screen,
						current_screen->vdesk + 1);
			}
			break;
#endif
		default: break;
	}
	c = current;
	if (c == NULL) return;
	width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	height_inc = (c->height_inc > 1) ? c->height_inc : 16;
	switch (key) {
		case KEY_LEFT:
			if (e->state & altmask) {
				if ((c->width - width_inc) >= c->min_width)
					c->width -= width_inc;
			} else {
				c->x -= 16;
			}
			goto move_client;
		case KEY_DOWN:
			if (e->state & altmask) {
				if (!c->max_height || (c->height + height_inc) <= c->max_height)
					c->height += height_inc;
			} else {
				c->y += 16;
			}
			goto move_client;
		case KEY_UP:
			if (e->state & altmask) {
				if ((c->height - height_inc) >= c->min_height)
					c->height -= height_inc;
			} else {
				c->y -= 16;
			}
			goto move_client;
		case KEY_RIGHT:
			if (e->state & altmask) {
				if (!c->max_width || (c->width + width_inc) <= c->max_width)
					c->width += width_inc;
			} else {
				c->x += 16;
			}
			goto move_client;
		case KEY_TOPLEFT:
			c->x = c->border;
			c->y = c->border;
			goto move_client;
		case KEY_TOPRIGHT:
			c->x = DisplayWidth(dpy, c->screen->screen)
				- c->width-c->border;
			c->y = c->border;
			goto move_client;
		case KEY_BOTTOMLEFT:
			c->x = c->border;
			c->y = DisplayHeight(dpy, c->screen->screen)
				- c->height-c->border;
			goto move_client;
		case KEY_BOTTOMRIGHT:
			c->x = DisplayWidth(dpy, c->screen->screen)
				- c->width-c->border;
			c->y = DisplayHeight(dpy, c->screen->screen)
				- c->height-c->border;
			goto move_client;
		case KEY_KILL:
			send_wm_delete(c, e->state & altmask);
			break;
		case KEY_LOWER: case KEY_ALTLOWER:
			XLowerWindow(dpy, c->parent);
			break;
		case KEY_INFO:
			show_info(c, key);
			break;
		case KEY_MAX:
			maximise_client(c, NET_WM_STATE_TOGGLE, MAXIMISE_HORZ|MAXIMISE_VERT);
			break;
		case KEY_MAXVERT:
			maximise_client(c, NET_WM_STATE_TOGGLE, MAXIMISE_VERT);
			break;
#ifdef VWM
		case KEY_FIX:
			fix_client(c, NET_WM_STATE_TOGGLE);
			break;
#endif
		default: break;
	}
	return;
move_client:
	if (abs(c->x) == c->border && c->oldw != 0)
		c->x = 0;
	if (abs(c->y) == c->border && c->oldh != 0)
		c->y = 0;
	moveresize(c);
	setmouse(c->window, c->width + c->border - 1,
			c->height + c->border - 1);
	discard_enter_events();
	return;
}

#ifdef MOUSE
static void handle_button_event(XButtonEvent *e) {
	Client *c = find_client(e->window);

	if (c) {
		switch (e->button) {
			case Button1:
				drag(c); break;
			case Button2:
				sweep(c); break;
			case Button3:
				XLowerWindow(dpy, c->parent); break;
			default: break;
		}
	}
}
#endif

static void handle_configure_request(XConfigureRequestEvent *e) {
	Client *c = find_client(e->window);
	XWindowChanges wc;
	unsigned int value_mask = e->value_mask;

	wc.sibling = e->above;
	wc.stack_mode = e->detail;
	wc.width = e->width;
	wc.height = e->height;
	if (c) {
		ungravitate(c);
		if (value_mask & CWWidth) c->width = e->width;
		if (value_mask & CWHeight) c->height = e->height;
		if (value_mask & CWX) c->x = e->x;
		if (value_mask & CWY) c->y = e->y;
		if (value_mask & CWStackMode && value_mask & CWSibling) {
			Client *sibling = find_client(e->above);
			if (sibling) {
				wc.sibling = sibling->parent;
			}
		}
		if (c->x == 0 && c->width >= DisplayWidth(dpy, c->screen->screen)) {
			c->x -= c->border;
		}
		if (c->y == 0 && c->height >= DisplayHeight(dpy, c->screen->screen)) {
			c->y -= c->border;
		}
		gravitate(c);

		wc.x = c->x - c->border;
		wc.y = c->y - c->border;
		wc.border_width = c->border;
		LOG_XENTER("XConfigureWindow(parent=%lx, value_mask=%lx)", (unsigned int)c->parent, value_mask);
		XConfigureWindow(dpy, c->parent, value_mask, &wc);
		LOG_XLEAVE();
		XMoveResizeWindow(dpy, c->window, 0, 0, c->width, c->height);
		if ((value_mask & (CWX|CWY)) && !(value_mask & (CWWidth|CWHeight))) {
			send_config(c);
		}
		wc.border_width = 0;
	} else {
		wc.x = c ? 0 : e->x;
		wc.y = c ? 0 : e->y;
		LOG_XENTER("XConfigureWindow(window=%lx, value_mask=%lx)", (unsigned int)e->window, value_mask);
		XConfigureWindow(dpy, e->window, value_mask, &wc);
		LOG_XLEAVE();
	}
}

static void handle_map_request(XMapRequestEvent *e) {
	Client *c = find_client(e->window);

	LOG_ENTER("handle_map_request(window=%lx)", e->window);
	if (c) {
#ifdef VWM
		if (c->vdesk != c->screen->vdesk)
			switch_vdesk(c->screen, c->vdesk);
#endif
		unhide(c, RAISE);
	} else {
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, e->window, &attr);
		make_new_client(e->window, find_screen(attr.root));
	}
	LOG_LEAVE();
}

static void handle_unmap_event(XUnmapEvent *e) {
	Client *c = find_client(e->window);

	LOG_ENTER("handle_unmap_event(window=%lx)", e->window);
	if (c) {
		if (c->ignore_unmap) {
			c->ignore_unmap--;
			LOG_DEBUG("ignored (%d ignores remaining)\n", c->ignore_unmap);
		} else {
			LOG_DEBUG("flagging client for removal\n");
			c->remove = 1;
			need_client_tidy = 1;
		}
	} else {
		LOG_DEBUG("unknown client!\n");
	}
	LOG_LEAVE();
}

static void handle_colormap_change(XColormapEvent *e) {
	Client *c = find_client(e->window);

	if (c && e->new) {
		c->cmap = e->colormap;
		XInstallColormap(dpy, c->cmap);
	}
}

static void handle_property_change(XPropertyEvent *e) {
	Client *c = find_client(e->window);

	if (c) {
		LOG_ENTER("handle_property_change(window=%lx, atom=%s)", e->window, debug_atom_name(e->atom));
		if (e->atom == XA_WM_NORMAL_HINTS) {
			get_wm_normal_hints(c);
			LOG_DEBUG("geometry=%dx%d+%d+%d\n", c->width, c->height, c->x, c->y);
		}
		LOG_LEAVE();
	}
}

static void handle_enter_event(XCrossingEvent *e) {
	Client *c;

	if ((c = find_client(e->window))) {
#ifdef VWM
		if (c->vdesk != c->screen->vdesk)
			return;
#endif
		select_client(c);
		current_to_head();
	}
}

static void handle_mappingnotify_event(XMappingEvent *e) {
	XRefreshKeyboardMapping(e);
	if (e->request == MappingKeyboard) {
		int i;
		for (i = 0; i < num_screens; i++) {
			grab_keys_for_screen(&screens[i]);
		}
	}
}

#ifdef SHAPE
static void handle_shape_event(XShapeEvent *e) {
	Client *c = find_client(e->window);
	if (c)
		set_shape(c);
}
#endif

static void handle_client_message(XClientMessageEvent *e) {
	ScreenInfo *s = find_current_screen();
	Client *c;

	LOG_ENTER("handle_client_message(window=%lx, format=%d, type=%s)", e->window, e->format, debug_atom_name(e->message_type));

#ifdef VWM
	if (e->message_type == xa_net_current_desktop) {
		switch_vdesk(s, e->data.l[0]);
		LOG_LEAVE();
		return;
	}
#endif
	c = find_client(e->window);
	if (!c && e->message_type == xa_net_request_frame_extents) {
		ewmh_set_net_frame_extents(e->window);
		LOG_LEAVE();
		return;
	}
	if (e->message_type == xa_net_wm_state) {
		int i, maximise_hv;
		/* Message can contain up to two state changes: */
		for (i = 1; i <= 2; i++) {
			if ((Atom)e->data.l[i] == xa_net_wm_state_sticky) {
				fix_client(c, e->data.l[0]);
			} else if ((Atom)e->data.l[i] == xa_net_wm_state_maximized_vert) {
				maximise_hv |= MAXIMISE_VERT;
			} else if ((Atom)e->data.l[i] == xa_net_wm_state_maximized_horz) {
				maximise_hv |= MAXIMISE_HORZ;
			} else if ((Atom)e->data.l[i] == xa_net_wm_state_fullscreen) {
				maximise_hv |= MAXIMISE_VERT|MAXIMISE_HORZ;
			}
			if (maximise_hv) {
				maximise_client(c, e->data.l[0], maximise_hv);
			}
		}
		LOG_LEAVE();
		return;
	}
	LOG_LEAVE();
}

void event_main_loop(void) {
	XEvent ev;
	/* main event loop here */
	while (!wm_exit) {
		if (interruptibleXNextEvent(&ev)) {
			switch (ev.type) {
			case KeyPress:
				handle_key_event(&ev.xkey); break;
#ifdef MOUSE
			case ButtonPress:
				handle_button_event(&ev.xbutton); break;
#endif
			case ConfigureRequest:
				handle_configure_request(&ev.xconfigurerequest); break;
			case MapRequest:
				handle_map_request(&ev.xmaprequest); break;
			case ColormapNotify:
				handle_colormap_change(&ev.xcolormap); break;
			case EnterNotify:
				handle_enter_event(&ev.xcrossing); break;
			case PropertyNotify:
				handle_property_change(&ev.xproperty); break;
			case UnmapNotify:
				handle_unmap_event(&ev.xunmap); break;
			case MappingNotify:
				handle_mappingnotify_event(&ev.xmapping); break;
			case ClientMessage:
				handle_client_message(&ev.xclient); break;
			default:
#ifdef SHAPE
				if (have_shape && ev.type == shape_event) {
					handle_shape_event((XShapeEvent *)&ev);
				}
#endif
#ifdef RANDR
				if (have_randr && ev.type == randr_event_base + RRScreenChangeNotify) {
					XRRUpdateConfiguration(&ev);
				}
#endif
				break;
			}
		}
		if (need_client_tidy) {
			Client *c, *nc;
			need_client_tidy = 0;
			for (c = head_client; c; c = nc) {
				nc = c->next;
				if (c->remove)
					remove_client(c);
			}
		}
	}
}

/* interruptibleXNextEvent() is taken from the Blender source and comes with
 * the following copyright notice: */

/* Copyright (c) Mark J. Kilgard, 1994, 1995, 1996. */

/* This program is freely distributable without licensing fees
 * and is provided without guarantee or warrantee expressed or
 * implied. This program is -not- in the public domain. */

/* Unlike XNextEvent, if a signal arrives, interruptibleXNextEvent will
 * return zero. */

static int interruptibleXNextEvent(XEvent *event) {
	fd_set fds;
	int rc;
	int dpy_fd = ConnectionNumber(dpy);
	for (;;) {
		if (XPending(dpy)) {
			XNextEvent(dpy, event);
			return 1;
		}
		FD_ZERO(&fds);
		FD_SET(dpy_fd, &fds);
		rc = select(dpy_fd + 1, &fds, NULL, NULL, NULL);
		if (rc < 0) {
			if (errno == EINTR) {
				return 0;
			} else {
				LOG_ERROR("interruptibleXNextEvent(): select()\n");
			}
		}
	}
}
