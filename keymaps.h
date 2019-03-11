/***************************************************************************
 * This program is Copyright (C) 1986, 1987, 1988 by Jonathan Payne.  JOVE *
 * is provided to you without charge, and with no warranty.  You may give  *
 * away copies of JOVE, including sources, provided that this notice is    *
 * included in all the files.                                              *
 ***************************************************************************/

struct keymap {
	int		Type;		/* keymap type */
	char		*Name;		/* keymap name */
	data_obj	**k_keys;	/* keys array */
	char		k_alloc_p;	/* whether we alloced k_keys */
};

extern data_obj	*MainKeys[NCHARS],
		*EscKeys[NCHARS],
		*CtlxKeys[NCHARS];

#ifdef MAC					/* used in About Jove... */
# define F_MAINMAP '\001'
# define F_PREF1MAP '\002'
# define F_PREF2MAP '\003'
#endif

extern int
	this_cmd,	/* ... */
	last_cmd;	/* last command ... to implement appending
			   to kill buffer */

extern void
	BindSomething proto((struct data_obj *(*proc)())),
	BindWMap proto((struct data_obj **map, int lastkey, struct data_obj *cmd)),
	DescMap proto((struct data_obj **map, char *pref)),
	DescWMap proto((struct data_obj **map, int key)),
	dispatch proto((int c));
