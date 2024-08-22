/*
 * Copyright (c) 2020 joshua stein <jcs@jcs.org>
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <strings.h>
#include "progman.h"

action_t *key_actions = NULL;
int nkey_actions = 0;
static int cycle_key = 0;

action_t *
bind_key(int type, char *key, char *action)
{
	char *tkey, *sep;
	KeySym k = 0;
	action_t *taction;
	int x, mod = 0, iaction = -1, button = 0, overwrite, aidx;

	tkey = strdup(key);

	/* key can be "shift+alt+f1" or "Super+Space" or just "ampersand" */
	while ((sep = strchr(tkey, '+'))) {
		*sep = '\0';
		if (strcasecmp(tkey, "shift") == 0)
			mod |= ShiftMask;
		else if (strcasecmp(tkey, "control") == 0 ||
		    strcasecmp(tkey, "ctrl") == 0 ||
		    strcasecmp(tkey, "ctl") == 0)
			mod |= ControlMask;
		else if (strcasecmp(tkey, "alt") == 0 ||
		    strcasecmp(tkey, "meta") == 0 ||
		    strcasecmp(tkey, "mod1") == 0)
			mod |= Mod1Mask;
		else if (strcasecmp(tkey, "mod2") == 0)
			mod |= Mod2Mask;
		else if (strcasecmp(tkey, "mod3") == 0)
			mod |= Mod3Mask;
		else if (strcasecmp(tkey, "super") == 0 ||
		    strcasecmp(tkey, "win") == 0 ||
		    strcasecmp(tkey, "mod4") == 0)
			mod |= Mod4Mask;
		else {
			warnx("failed parsing modifier \"%s\" in \"%s\", "
			    "skipping", tkey, key);
			return NULL;
		}

		tkey = sep + 1;
	}

	/* modifiers have been parsed, only the key or button should remain */
	if (strncasecmp(tkey, "mouse", 5) == 0 &&
	    tkey[5] > '0' && tkey[5] <= '9')
		button = tkey[5] - '0';
	else if (tkey[0] != '\0') {
		if (tkey[1] == '\0') {
			/*
			 * Assume a single-character key is meant to be used as
			 * its lower-case key, e.g., "Win+T" is mod4+t, not
			 * mod4+T, and if the user wanted it a capital t, they
			 * would specify it as "Win+Shift+T"
			 */
			tkey[0] = tolower(tkey[0]);
		}

		k = XStringToKeysym(tkey);
		if (k == 0) {
			warnx("failed parsing key \"%s\", skipping\n", tkey);
			return NULL;
		}
	}

	/* action can be e.g., "cycle" or "exec xterm -g 80x50" */
	taction = parse_action(key, action);
	if (taction == NULL)
		return NULL;

	/* if we're overriding an existing config, replace it in key_actions */
	overwrite = 0;
	for (x = 0; x < nkey_actions; x++) {
		if (key_actions[x].type == type &&
		    key_actions[x].key == k &&
		    key_actions[x].mod == mod &&
		    key_actions[x].button == button) {
			overwrite = 1;
			aidx = x;
			break;
		}
	}

	if (!overwrite) {
		key_actions = realloc(key_actions,
		    (nkey_actions + 1) * sizeof(action_t));
		if (key_actions == NULL)
			err(1, "realloc");

		aidx = nkey_actions;
	}

	if (taction->action == ACTION_NONE) {
		key_actions[aidx].key = -1;
		key_actions[aidx].mod = -1;
		key_actions[aidx].button = 0;
	} else {
		key_actions[aidx].key = k;
		key_actions[aidx].mod = mod;
		key_actions[aidx].button = button;
	}
	key_actions[aidx].type = type;
	key_actions[aidx].action = taction->action;
	key_actions[aidx].iarg = taction->iarg;
	if (overwrite && key_actions[aidx].sarg)
		free(key_actions[aidx].sarg);
	key_actions[aidx].sarg = taction->sarg;

	/* retain taction->sarg */
	free(taction);

	if (!overwrite)
		nkey_actions++;

#ifdef DEBUG
	if (key_actions[aidx].action == ACTION_NONE)
		printf("%s(%s): unbinding key %ld/button %d with "
		    "mod mask 0x%x\n", __func__, key, k, button, mod);
	else
		printf("%s(%s): binding key %ld/button %d with mod mask 0x%x "
		    "to action \"%s\"\n", __func__, key, k, button, mod,
		    action);
#endif

	if (type == BINDING_TYPE_KEYBOARD) {
		if (overwrite && iaction == ACTION_NONE)
			XUngrabKey(dpy, XKeysymToKeycode(dpy, k), mod, root);
		else if (!overwrite)
			XGrabKey(dpy, XKeysymToKeycode(dpy, k), mod, root,
			    False, GrabModeAsync, GrabModeAsync);
	}

	return &key_actions[aidx];
}

void
handle_key_event(XKeyEvent *e)
{
#ifdef DEBUG
	char buf[64];
#endif
	KeySym kc;
	action_t *action = NULL;
	int i;

	kc = XLookupKeysym(e, 0);

#ifdef DEBUG
	snprintf(buf, sizeof(buf), "%c:%ld", e->type == KeyRelease ? 'U' : 'D',
	    kc);
	dump_name(focused, __func__, buf, NULL);
#endif

	if (cycle_key && kc != cycle_key && e->type == KeyRelease) {
		/*
		 * If any key other than the non-modifier(s) of our cycle
		 * binding was released, consider the cycle over.
		 */
		cycle_key = 0;
		XUngrabKeyboard(dpy, CurrentTime);
		XAllowEvents(dpy, ReplayKeyboard, e->time);
		XFlush(dpy);

		if (cycle_head) {
			cycle_head = NULL;
			if (focused && focused->state & STATE_ICONIFIED)
				uniconify_client(focused);
		}

		return;
	}

	for (i = 0; i < nkey_actions; i++) {
		if (key_actions[i].type == BINDING_TYPE_KEYBOARD &&
		    key_actions[i].key == kc &&
		    key_actions[i].mod == e->state) {
			action = &key_actions[i];
			break;
		}
	}

	if (!action)
		return;

	if (e->type != KeyPress)
		return;

	switch (key_actions[i].action) {
	case ACTION_CYCLE:
	case ACTION_REVERSE_CYCLE:
		/*
		 * Keep watching input until the modifier is released, but the
		 * keycode will be the modifier key
		 */
		XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
		    e->time);
		cycle_key = key_actions[i].key;
		take_action(&key_actions[i]);
		break;
        case ACTION_ICONIFY:
                if(focused)
                        iconify_client(focused);
                break;
        case ACTION_FULL_SCREEN:
                static bool is_fullscreen;
                if(focused)
                        if(is_fullscreen = !is_fullscreen)
                                fullscreen_client(focused);
                        else
                                unfullscreen_client(focused);
                break;
	default:
		take_action(&key_actions[i]);
	}
}
