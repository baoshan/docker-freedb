/*
 *   cddbd - CD Database Protocol Server
 *
 *   Copyright (C) 1996-1997  Steve Scherf (steve@moonsoft.com)
 *   Portions Copyright (C) 2001  by various authors
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef __LIST_H__
#define __LIST_H__

#ifndef LINT
static char *const _list_h_ident_ = "@(#)$Id: list.h,v 1.7.2.1 2006/04/06 17:20:40 megari Exp $";
#endif


/* Structures. */

typedef struct link {
	struct link	*l_forw;	/* position must match lh_list in lhead_t */
	struct link	*l_back;	/* position must match lh_cur in lhead_t  */
	void		*l_data;
} link_t;


typedef struct link_head {
	link_t	lh_list;		/* position must match l_forw in link_t */
	link_t	*lh_cur;		/* position must match l_back in link_t */
	int	lh_count;
	int	(*lh_comp)(void *, void *);
	void	(*lh_free)(void *);
	void	(*lh_hfree)(void *);
	void	*lh_data;
} lhead_t;


/* Preprocessor definitions. */

#define list_count(lh)		((lh)->lh_count)
#define list_empty(lh)		((lh)->lh_count == 0)
#define list_rewind(lh)		((lh)->lh_cur = (link_t *)(lh))
#define list_rewound(lh)	((lh)->lh_cur == (link_t *)(lh))
#define list_cur(lh)		((lh)->lh_cur)
#define list_next(lh)		((lh)->lh_cur->l_forw)
#define list_last(lh)		((lh)->lh_cur->l_back)
#define list_forw(lh)		((lh)->lh_cur = (lh)->lh_cur->l_forw)
#define list_back(lh)		((lh)->lh_cur = (lh)->lh_cur->l_back)
#define list_first(lh)		((lh)->lh_list.l_forw)


/* External prototypes. */

lhead_t *list_init(void *, int (*)(void *, void *), void (*)(void *),
    void (*)(void *));

link_t *list_find(lhead_t *, void *);
link_t *list_add_cur(lhead_t *, void *);
link_t *list_add_forw(lhead_t *, void *);
link_t *list_add_back(lhead_t *, void *);

void list_free(lhead_t *);
void list_delete(lhead_t *, link_t *);
#endif
