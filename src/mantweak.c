#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>

// openfont(2) runestringnwidth(2) /sys/src/
#define USAGE "usage: %s [-t tabstop] [-l levels] [-n] [-f font]\n"

int levels;
int addnl;	/* -n */
int tabstop;
Font *font;	/* $font of -f font, but nil if -f does not have an path */

enum LineType 
{
	Empty,			/* only spaces, tabs or newline */
	Text,
	SectionTitle,
	Table
};

typedef struct Line Line;
struct Line
{
	enum LineType type;
	int initialspaces;
	int margin;
	int len;			/* length in chars */
	char *raw;
	char *content;
};

typedef struct Column Column;
struct Column
{
	int size;			/* number of Runes */
	int width;			/* pixels */
	Column *next;
};
typedef struct Row Row;
struct Row
{
	Line *line;
	Row *prev;
};

int *margins;		/* spaces for each level of margin */
Line *prev; 		/* previous line read */
Column *tStruct;	/* structure of the current table (if any) */
Row *table;		/* linked list of table's line read (from the last) */





void
debugLine(Line *l, char *topic)
{
	fprint(2, "\t%s\n", topic);
	fprint(2, "l->content		%s", l->content);
	fprint(2, "l->margin		%d\n", l->margin);
	fprint(2, "l->len			%d\n", l->len);
	//fprint(2, "l->level			%d\n", l->level);
	fprint(2, "l->type		%d\n", l->type);
	fprint(2, "l->initialspaces	%d\n", l->initialspaces);
		for(int i = 0; i < levels; ++i)
			fprint(2, "margins[%d]		%d\n", i, margins[i]);
		fprint(2, "\n");
}

void
setMarginSize(int level, int spaces) {
	margins[level] = spaces;
	if(!addnl)
		/*	by subtracting level to margins[level]
			we add a single space for each margin level when 
			we don't replace indentation with \n (addnl == 0)
		*/
		margins[level] -= level;
}

/*	updates margins[] and set l->mrgin according.
 */
void
setMargin(Line *l)
{
	int i, j;

	switch(l->type){
	case Empty:
		/* initialspaces are ignored by writeLine on Empty lines */
		return;
	case SectionTitle:
		if(margins[0] == 0 || l->initialspaces < margins[0]){
			margins[0] = l->initialspaces;
		}
	case Table:
		if(prev && prev->type == Table)
			l->margin = prev->margin;
	case Text:
		i = 0;
		while(!l->margin && i < levels){
			if(margins[i] == 0){
				setMarginSize(i, l->initialspaces);
				l->margin = margins[i];
			}else if(margins[i] > l->initialspaces){
				/* move known stops forward */
				for(j = levels - 1; j > i; --j)
					margins[j] = margins[j-1];
				/* introduce the new stop */
				setMarginSize(i, l->initialspaces);
				l->margin = margins[i];
			}else if (margins[i] == l->initialspaces){
				/* reset next levels */
				for(j = levels - 1; j > i; --j)
					margins[j] = 0;
				l->margin = margins[i];
			}else
				++i;
		}
		
		if(!l->margin)
			l->margin = margins[levels - 1];
		break;
	}
}


void
freeTable(Column *t)
{
	if(t->next)
		freeTable(t->next);
	free(t);
}

Line *
readLine(Biobufhdr *bp)
{
	Line *l;
	char *c;
	Rune r, p;
	
	if (c = Brdstr(bp, '\n', 0)) {
		l = (Line *)malloc(sizeof(Line));
		l->raw = c;
		l->len = Blinelen(bp);
		l->type = Empty;
		l->content = nil;
		l->initialspaces = 0;
		l->margin = 0;
		
		while(*c && !l->content){
			switch(*c){
			case '\n':
				l->content = c;
				break;
			case ' ':
				l->initialspaces++;
				break;
			case '\t':
				l->initialspaces += tabstop - (l->initialspaces % tabstop);
				break;
			default:
				l->content = c;
				break;
			}
			if(!l->content)
				++c;
		}

		l->len -= l->content - l->raw;
		if(l->len > 1){
			/*	SectionTitles = lines without lowercase runes 
				Table = lines with tabs or multiple subsequent spaces 
				Text = anything else
			 */
			r = 0; p = 0;
			l->type = SectionTitle;
			while(*c && l->type != Table){
				c += chartorune(&r, c);
				if (r == '\t' || (isspacerune(r) && isspacerune(p)))
					l->type = Table;
				else if(l->type == SectionTitle && islowerrune(r))
					l->type = Text;
				p = r;
			}
		}
		//debugLine(l, "readLine");
	} else
		l = nil;
		
	return l;
}

void 
_writeLine(Biobufhdr *bp, Line *l) {
	int i;
	i = l->initialspaces - l->margin;
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

// TODO: collapse writeLine and _writeLine
void 
writeLine(Biobufhdr *bp, Line *l) {
	switch(l->type){
	case Empty:
		//fprint(2, "writeLine: added new line for Empty\n");
		Bputc(bp, '\n');
		break;
	case SectionTitle:
		_writeLine(bp, l);
		break;
	case Text:
	case Table:
		_writeLine(bp, l);
		break;
	}
}

void 
freeLine(Line * ln) {
	free(ln->raw);
	free(ln);
}

static void
usage(char *error)
{
	if(error != nil)
		fprint(2, "invalid argument: %s\n", error);
	fprint(2, USAGE, argv0);
	exits("usage");
}

void
parseArguments(int argc, char **argv)
{
	char *s;

	levels = 1;
	addnl = 0;
	
	if(s = getenv("tabstop")){
		tabstop = atoi(s);
		free(s);
	} else {
		tabstop = 8;
	}

	s = getenv("font");
	if(s != nil){
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
		if(font){
			/* an empty -f flag means no font adjustments */
			free(font);
			font = nil;
		}
		if(s){
			font = openfont(nil, s);
			if(!font)
				usage("can't open font file.");
		}
	default:
		usage(nil);
	}ARGEND;
}

#define INDENTATIONCOLLAPSED(l,p) (p && l->type != Empty && \
		l->type != p->type && \
		l->initialspaces - l->margin == p->initialspaces - p->margin)

#define TABLECONTINUES(l,p) (p && p->type == Table && \
		l->type != Empty && \
		l->initialspaces >= prev->initialspaces)

void
main(int argc, char **argv)
{
	Biobuf bin, bout;
	Line *l;

	parseArguments(argc, argv);
	//fprint(2, "tabstop = %d\n\n", tabstop);

	margins = (int*)calloc(levels, sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		if(TABLECONTINUES(l, prev))
			l->type = Table;
		setMargin(l);
		//debugLine(l, "setMargin");
		if(addnl && INDENTATIONCOLLAPSED(l, prev)) {
			//fprint(2, "main: added new line for addnl\n");
			Bputc(&bout, '\n');
		}
		writeLine(&bout, l);
		if (table && l->type != Table) {
			//freeTable(table);
			table = nil;
		}
		if(prev){
			freeLine(prev);
			prev = nil;
		}
		if(l->type != Empty)
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