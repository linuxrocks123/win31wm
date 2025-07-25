/*
 * Copyright 2020 joshua stein <jcs@jcs.org>
 * Copyright 1998-2007 Decklin Foster <decklin@red-bean.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include "progman.h"

void
fork_exec(char *cmd)
{
	pid_t pid = fork();

	switch (pid) {
	case 0:
		setsid();
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		fprintf(stderr, "exec failed, cleaning up child\n");
		exit(1);
	case -1:
		fprintf(stderr, "can't fork\n");
	}
}

int
get_pointer(int *x, int *y)
{
	Window real_root, real_win;
	int wx, wy;
	unsigned int mask;

	XQueryPointer(dpy, root, &real_root, &real_win, x, y, &wx, &wy, &mask);
	return mask;
}

int
send_xmessage(Window t, Window w, Atom a, unsigned long x, unsigned long mask)
{
	XClientMessageEvent e;

	e.type = ClientMessage;
	e.window = w;
	e.message_type = a;
	e.format = 32;
	e.data.l[0] = x;
	e.data.l[1] = CurrentTime;

	return XSendEvent(dpy, t, False, mask, (XEvent *)&e);
}

action_t *
parse_action(char *prefix, char *action)
{
	char *taction = NULL, *targ = NULL;
	char *sep;
	char *sarg = NULL;
	action_t *out = NULL;
	int iaction = ACTION_NONE;
	int iarg = 0;

	taction = strdup(action);
	if ((sep = strchr(taction, ' '))) {
		*sep = '\0';
		targ = sep + 1;
	} else
		targ = NULL;

	if (strcmp(taction, "cycle") == 0)
		iaction = ACTION_CYCLE;
	else if (strcmp(taction, "reverse_cycle") == 0)
		iaction = ACTION_REVERSE_CYCLE;
	else if (strcmp(taction, "desk") == 0)
		iaction = ACTION_DESK;
	else if (strcmp(taction, "close") == 0)
		iaction = ACTION_CLOSE;
	else if (strcmp(taction, "exec") == 0)
		iaction = ACTION_EXEC;
	else if (strcmp(taction, "launcher") == 0)
		iaction = ACTION_LAUNCHER;
	else if (strcmp(taction, "restart") == 0)
		iaction = ACTION_RESTART;
	else if (strcmp(taction, "quit") == 0)
		iaction = ACTION_QUIT;
	else if (strcmp(taction, "drag") == 0)
		iaction = ACTION_DRAG;
        else if (strcmp(taction, "fullscreen") == 0)
                iaction = ACTION_FULL_SCREEN;
        else if (strcmp(taction,"iconify") == 0)
                iaction = ACTION_ICONIFY;
        else if (strcmp(taction,"move") == 0)
                iaction = ACTION_MOVE;
	else if (taction[0] == '\n' || taction[0] == '\0')
		iaction = ACTION_NONE;
	else
		iaction = ACTION_INVALID;

	/* parse numeric or string args */
	switch (iaction) {
	case ACTION_DESK:
        case ACTION_MOVE:
		if (targ == NULL) {
			warnx("%s: missing argument for \"%s\"",
			    prefix, taction);
			goto done;
		}

		if (strcmp(targ, "next") == 0)
                        iaction++;
		else if (strcmp(targ, "previous") == 0)
			iaction+=2;
                else if (strcmp(targ, "all") == 0)
                        iarg = DESK_ALL;
		else {
			errno = 0;
			iarg = strtol(targ, NULL, 10);
			if (errno != 0) {
				warnx("%s: failed parsing numeric argument "
				    "\"%s\" for \"%s\"", prefix, targ, taction);
				goto done;
			}
		}
		break;
	case ACTION_EXEC:
		if (targ == NULL) {
			warnx("%s: missing string argument for \"%s\"", prefix,
			    taction);
			goto done;
		}
		sarg = strdup(targ);
		break;
	case ACTION_INVALID:
		warnx("%s: invalid action \"%s\"", prefix, taction);
		goto done;
	default:
		/* no args expected of other commands */
		if (targ != NULL) {
			warnx("%s: unexpected argument \"%s\" for \"%s\"",
			    prefix, taction, targ);
			goto done;
		}
	}

	out = malloc(sizeof(action_t));
	out->action = iaction;
	out->iarg = iarg;
	out->sarg = sarg;

done:
	if (taction)
		free(taction);

	return out;
}

void
take_action(action_t *action)
{
	client_t *p, *next;

	switch (action->action) {
	case ACTION_CYCLE:
	case ACTION_REVERSE_CYCLE:
		if (!cycle_head) {
			if (!focused)
				return;

			cycle_head = focused;
		}

		if ((next = next_client_for_focus(cycle_head)))
			focus_client(next, FOCUS_FORCE);
		else {
			/* probably at the end of the list, invert it */
			p = focused;
			adjust_client_order(NULL, ORDER_INVERT);

			if (p)
				/* p should now not be focused */
				redraw_frame(p, None);

			focus_client(cycle_head, FOCUS_FORCE);
		}
		break;
	case ACTION_DESK:
		goto_desk(action->iarg);
		break;
	case ACTION_DESK_NEXT:
		if (cur_desk < ndesks - 1)
			goto_desk(cur_desk + 1);
		break;
	case ACTION_DESK_PREVIOUS:
		if (cur_desk > 0)
			goto_desk(cur_desk - 1);
		break;
        case ACTION_MOVE_NEXT:
        case ACTION_MOVE_PREVIOUS:
                action->iarg = cur_desk + (action->action==ACTION_MOVE_NEXT ? 1 : -1);
	case ACTION_MOVE:
                if(focused)
                {
                        int old_desk = focused->desk;
                        int new_desk = action->iarg;
                        if(old_desk==new_desk)
                                break;
                        focused->desk = new_desk;
                        if(new_desk==DESK_ALL)
                                break;
                        int screen_idx;
                        for(screen_idx=0; screen_idx < num_xinerama_screens; screen_idx++)
                                if(shown_desks[screen_idx]==new_desk)
                                        break;
                        if(screen_idx==num_xinerama_screens)
                        {
                                XUnmapWindow(dpy,focused->frame);
                                break;
                        }

                        if(xinerama_move_client_if_needed(focused,xinerama_screens[xinerama_screen_idx],xinerama_screens[screen_idx]))
                        {
                                cur_desk = new_desk;
                                redraw_frame(focused,None);
                                send_config(focused);
                                flush_expose_client(focused);
                        }
                }
		break;
	case ACTION_CLOSE:
		if (focused)
			send_wm_delete(focused);
		break;
	case ACTION_EXEC:
		fork_exec(action->sarg);
		break;
	case ACTION_LAUNCHER:
		launcher_show(NULL);
		break;
	case ACTION_RESTART:
		cleanup();
		execlp(orig_argv0, orig_argv0, NULL);
		break;
	case ACTION_QUIT:
		quit();
		break;
	default:
		warnx("unhandled action %d\n", action->action);
	}
}

geom_t get_geometry(Display* dpy)
{
        geom_t to_return;
        if (XineramaIsActive (dpy))
        {
                int m = 0;
                int pixels = 0;
                
                XineramaScreenInfo *xs = XineramaQueryScreens (dpy, &m);

                if(xs && xinerama_screen_idx < m)
                {
                        to_return.x = xs[xinerama_screen_idx].x_org;
                        to_return.y = xs[xinerama_screen_idx].y_org;
                        to_return.w = xs[xinerama_screen_idx].width;
                        to_return.h = xs[xinerama_screen_idx].height;
                }
                else if (0 != xs && m > 0)
                        for (int i = 0; i < m; i++)
                        {
                                //printf ("%dx%d, [%d, %d] %d\n", xs[i].width, xs[i].height, xs[i].x_org, xs[i].y_org, xs[i].screen_number);
                                if (xs[i].width * xs[i].height > pixels)
                                {
                                        pixels = xs[i].width * xs[i].height;
                                        to_return.x = xs[i].x_org;
                                        to_return.y = xs[i].y_org;
                                        to_return.w = xs[i].width;
                                        to_return.h = xs[i].height;
                                }
                        }
                
                if(xs)
                        XFree (xs);
        }
        else
        {
                to_return.x = to_return.y = 0;
                to_return.w = DisplayWidth(dpy,screen);
                to_return.h = DisplayHeight(dpy,screen);
        }

        return to_return;
}

struct Dimensions get_dimensions(Display* dpy, int screen)
{
        geom_t dpy_geom = get_geometry(dpy);
        return (struct Dimensions){dpy_geom.w,dpy_geom.h};
}

int get_x(Display* dpy, int screen)
{
        return get_dimensions(dpy,screen).width;
}

int get_y(Display* dpy, int screen)
{
        return get_dimensions(dpy,screen).height;
}

int get_max_overlap_desk(client_t* c)
{
        return shown_desks[get_max_overlap_xinerama_screen(c)];
}

int get_max_overlap_xinerama_screen(client_t* c)
{
        int max_overlap = 0;
        int max_overlap_screen = 0;
        for(int i=0; i<num_xinerama_screens; i++)
        {
                int current_overlap = overlapping_area(c->geom,xinerama_screens[i]);
                if(current_overlap > max_overlap)
                {
                        max_overlap = current_overlap;
                        max_overlap_screen = i;
                }
        }
        return max_overlap_screen;
}
