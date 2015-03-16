#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>

// Edit ,s/margins/margins/g
// openfont(2) runestringnwidth(2) fullrune(2) /sys/src/

#define USAGE "usage: %s [-t tabstop] [-l levels] [-n] [-f font]\n"
typedef struct Line Line;
struct Line
{
	int level;
	int initialspaces;
	int table;
	int len;
	char *raw;
	char *content;
};

typedef struct Column Column;
struct Column
{
	int position;
	Column *next;
};


int levels;
int addnl;
int tabstop;

Font *font;
int *margins;		/* spaces for each level of margin */
Line *prev; 		/* previous line read */
Column *table;	/* structure of the current table (if any) */

static void
usage(char *error) {
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	fprint(2, USAGE, argv0);
	exits("usage");
}


int
margin(Line *l) {
	if (l->initialspaces >= margins[l->level])
		return margins[l->level];
	return l->initialspaces;
}

void
updatemargin(int level, int spaces) {
	margins[level] = spaces;
	if(!addnl)
		margins[level] -= level;
}

int
getlevel(int initialspaces) {
	int i, l;
	
	for(i = 0; i < levels; ++i) {
		if(margins[i] == 0) {
			updatemargin(i, initialspaces);
			return i;
		}else if (margins[i] > initialspaces) {
			/* move known stops forward */
			for(l = levels - 1; l > i; --l)
				margins[l] = margins[l-1];
			/* introduce the new stop */
			updatemargin(i, initialspaces);
			return i;
		}else if (margins[i] == initialspaces) {
			/* reset next levels */
			for(l = levels - 1; l > i; --l)
				margins[l] = 0;
			return i;
		}
	}
	return levels - 1;
}

char *
findNextColumn(Line *l, char *curr) {
	Column * last;

	if (!table) {
		table = (Column*)malloc(sizeof(Column));
		table->position = 0;
		table->next = nil;
	}
	last = table;
	while(last->next)
		last=last->next;
	last->next = (Column *)malloc(sizeof(Column));
	last = last->next;
	last->position = curr - l->content;
	last->next = nil;
	while(isspace(*curr) || ispunct(*curr))
		if (*curr++ == '\t') 
			last->position += tabstop - last->position % tabstop;
		else
			last->position++;
	return curr-1;	/* readline will increment with chartorune(2) */
}

int
isSectionHeader(Line * l) {
	char *c;

	if (l->level > 0 || l->len < 2)
		return 0;
	c = l->content;
	while(*c && (isspace(*c) || isupper(*c)))
		++c;
	return *c == 0;
}

int
checkTable(Line *l) {
	Column *col;
	char *c;
	int p;
	Rune r;

	col = table;
	if (table && l->len > 1) {
		p = 0;
		c = l->content;
		do
		{
			if(*c == '\t')
				p += tabstop - p % tabstop;
			else 
				p++;
			if (p == col->next->position - 1)
				if (!isspace(*c) && !ispunct(*c))
					return 0;
				else
					col = col->next;
			c += chartorune(&r, c);
		}
		while(*c && col && col->next);
	}
	return table && (!col->next || !isSectionHeader(l));
}

void
freeTable(Column *t) {
	if(t->next)
		freeTable(t->next);
	free(t);
}

Line *
readLine(Biobufhdr *bp) {
	Line *l;
	char *c;
	Rune r;
	
	if (c = Brdstr(bp, '\n', 0)) {
		l = (Line *)malloc(sizeof(Line));
		l->raw = c;
		l->len = Blinelen(bp);
		l->level = 0;
		l->table = 0;
		l->content = nil;
		l->initialspaces = 0;
		
		while (*c && !l->table) {
			switch(*c) {
			case '\n':
				if (!l->content)
					l->content = c;
				break;
			case ' ':
				if (!l->content)
					l->initialspaces++;
				else if (*(c-1) == ' ') {
					if (!prev->table)
						c = findNextColumn(l, c);
					else
						l->table = 1;
				}
				break;
			case '\t':
				if (!l->content)
					l->initialspaces += tabstop - (l->initialspaces % tabstop);
				else if (!prev->table)
					c = findNextColumn(l, c);
				else
					l->table = 1;
				break;
			default:
				if (!l->content)
					l->content = c;
				break;
			}
			c += chartorune(&r, c);
		}
		l->len -= l->content - l->raw;
		l->table = checkTable(l);
		if (l->table && prev && prev->table)
			l->level = prev->level;
		else if (l->len > 1)
			l->level = getlevel(l->initialspaces);
		l->initialspaces -= margin(l);
		/* *
		fprint(2, "l->content		%s", l->content);
		fprint(2, "l->len			%d\n", l->len);
		fprint(2, "l->level			%d\n", l->level);
		fprint(2, "l->table		%d\n", l->table);
		fprint(2, "l->initialspaces	%d\n", l->initialspaces);
		for(int i = 0; i < levels; ++i)
			fprint(2, "margins[%d]		%d\n", i, margins[i]);
		fprint(2, "\n");
		* */
		return l;
	}
	return nil;
}

void 
writeLine(Biobufhdr *bp, Line *l) {
	int i;

	i = l->initialspaces;
	if (l->len) {
		while(i >= tabstop) {
			Bputc(bp, '\t');
			i -= tabstop;
		}
		while(i-- > 0)
			Bputc(bp, ' ');
		Bwrite(bp, l->content, l->len);
		Bflush(bp);
	}	
}

void 
freeLine(Line * ln) {
	free(ln->raw);
	free(ln);
}

void
main(int argc, char **argv) {
	Biobuf bin, bout;
	char *s;
	Line *l;

	levels = 1;
	addnl = 0;
	
	if(s = getenv("tabstop")){
		tabstop = atoi(s);
		free(s);
	} else {
		tabstop = 8;
	}

	s = getenv("font");
	if (s != nil) {
		font = openfont(nil, s);
		free(s);
	} else
		font = nil;

	ARGBEGIN{
	case 't':
		s=ARGF();
		tabstop=atoi(s);
		if(tabstop < 2)
			usage("tabstop must be a greater than 1.");
		break;
	case 'l':
		s=ARGF();
		levels=atoi(s);
		if(levels < 1)
			usage("levels must be greater than 1.");
		break;
	case 'n':
		addnl = 1;
		break;
	case 'f':
		s = ARGF();
		if (font) {
			free(font);
			font = nil;
		}
		if (s) {
			font = openfont(nil, s);
			if(!font)
				usage("can't open font file.");
		}
	default:
		usage(nil);
	}ARGEND;

	//fprint(2, "tabstop = %d\n\n", tabstop);

	margins = (int*)calloc(levels, sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		if(prev && levels > 1) {
			if(!prev->level && !l->level && l->len > 1) {
			/*	section names hold one line, any successive 
				line with the same margin is part of the 
				section body.
				We change the first level of margin so that 
				the following lines will be correct
			*/
				margins[1] = margins[0];
				margins[0] = 0;
				l->level++;
			}
			if(addnl && prev->level < l->level && !prev->table)
				Bputc(&bout, '\n');
		}
		writeLine(&bout, l);
		if (table && !l->table) {
			freeTable(table);
			table = nil;
		}
		if (isSectionHeader(l)) {
			for(int i = levels; i > 0; --i)
				margins[i] = 0;
		}
		if(prev){
			freeLine(prev);
			prev = nil;
		}
		if(l->len > 1)
			prev = l;
		else
			freeLine(l);
	}
/*
	Column *t = table;
	int j = 0;
	while(table) {
		fprint(2, "column[%d]		%d\n", j++, table->position);
		table = table->next;
	}

	freeTable(t);
*/
	exits(nil);
}
