#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>
#include <draw.h>

// openfont(2) runestringnwidth(2) utflen(2)
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
	TableLine
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

typedef void (*LineHandler)(Biobufhdr *bp, Line *);

typedef struct Column Column;
struct Column
{
	int size;		/* number of Runes */
	int width;	/* pixels */
	Column *next;
};
typedef struct Row Row;
struct Row
{
	Line *line;
	Row *next;
};
typedef struct Table Table;
struct Table
{
	int nrows;		/* rows count */
	Row *rows;		/* linked list of rows */
	Column *columns;	/* linked list of detected columns */
	int maxutflen;		/* max utflen(2) of lines (used in detectColumns) */
};

int *margins;		/* spaces for each level of margin */
Line *prev; 		/* previous line read */
Table *table;		/* current table (if any) */

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

int
runeWidth(Rune r){
	static int *cache;
	Rune i;
	
	assert(font != nil);
	
	if(cache == nil){
		cache = (int *)calloc(Runeself, sizeof(int));
		for(i = 32; i < Runeself; ++i)
			cache[i] = runestringnwidth(font, &i, 1);
		fprint(2, "runeWidth: cache initialized\n");
	}
	if(r < Runeself)
		return cache[r];
	return runestringnwidth(font, &r, 1);
}

void
updateTable(Line *line){
	static Row *last;
	Row *newRow;
	int llen;

	assert(line->type == TableLine);

	//debugLine(line, "updateTable");

	newRow = (Row *)malloc(sizeof(Row));
	newRow->line = line;
	newRow->next = nil;
	if(table == nil){
		table = (Table *)malloc(sizeof(Table));
		table->nrows = 1;
		table->maxutflen = 0;
		table->columns = nil;
		table->rows = newRow;
	}else{
		last->next = newRow;
		++table->nrows;
	}
	last = newRow;
	llen = utflen(line->content);
	if(table->maxutflen < llen)
		table->maxutflen = llen;
}

Column *
detectColumns(Row *rows, int maxutflen){
	int *nonspaces;	/* nonspaces[i] == 0 => i'th rune separate words in all rows */
	int *runewidths;	/* max pixel width of runes at i at each row */
	Rune r;
	char *c;
	int i, j, w, consumed;
	Column *list, *last;
	
	nonspaces = (int *)calloc(maxutflen, sizeof(int));
	runewidths = (int *)calloc(maxutflen, sizeof(int));
	while(rows){
		i = 0;
		c = rows->line->content;
		while(*c){
			consumed = chartorune(&r, c);
			if(r == '\t'){
				// all widths up to tabstop must be at equal to 1em
				w = runeWidth(0x2003);
				for(j = i; j % tabstop > 0; ++j)
					if(runewidths[j] < w)
						runewidths[j] = w;
			}else{
				w = runeWidth(r);
				if(runewidths[i] < w)
					runewidths[i] = w;
			}
			if(nonspaces[i] == 0){
				if(consumed == 1){
					if(*c == '\t')
						/* we have a few spaces to skip */
						i += tabstop - i % tabstop;
					else
						nonspaces[i++] = !ispunct(*c) && !isspace(*c);
				}else
					nonspaces[i++] = !isspacerune(r);
			}else
				++i;		/* already found a word character in i */
			
			c += consumed;
		}
		rows = rows->next;
	}
	
	list = (Column *)malloc(sizeof(Column));
	list->size = 0;
	list->width = 0;
	list->next = nil;
	last = list;
	
	for(i = 0; i < maxutflen - 1; ++i){
		++last->size;
		last->width += runewidths[i];
		if(!nonspaces[i] && nonspaces[i+1]){
			/* a new column starts at i+1 */
			last->next = (Column *)malloc(sizeof(Column));
			last = last->next;
			last->size = 0;
			last->width = 0;
			last->next = nil;
		}
	}
	
	return list;
}

void
writeTable(void){

	if(!table->columns)
		table->columns = detectColumns(table->rows, table->maxutflen);

	debugLine(table->rows->line, "writeTable");
	fprint(2, "table->nrows = %d,	table->maxutflen = %d\n",table->nrows,table->maxutflen);

	Column *t = table->columns;
	int j = 0;
	while(t) {
		fprint(2, "column[%d]	size: %d,	width: %d\n", j++, t->size, t->width);
		t = t->next;
	}
}

void 
freeLine(Line * ln) {
	free(ln->raw);
	free(ln);
}

void
freeTable(void)
{
	void *p;
	Column *c;
	Row *r;
	
	c = table->columns;
	while(c){
		p = c;
		c = c->next;
		free(p);
	}
	r = table->rows;
	while(r){
		freeLine(r->line);
		p = r;
		r = r->next;
		free(p);
	}
	free(table);
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
	case TableLine:
		if(prev && prev->type == TableLine)
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
				TableLine = lines with tabs or multiple subsequent spaces 
				Text = anything else
			 */
			r = 0; p = 0;
			l->type = SectionTitle;
			while(*c && l->type != TableLine){
				c += chartorune(&r, c);
				if (r == '\t' || (isspacerune(r) && isspacerune(p)))
					l->type = TableLine;
				else if(l->type == SectionTitle && islowerrune(r))
					l->type = Text;
				p = r;
			}
		}else
			l->initialspaces = 0;
		//debugLine(l, "readLine");
	} else
		l = nil;
		
	return l;
}

void 
writeLine(Biobufhdr *bp, Line *l) {
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
/*
void 
writeLine(Biobufhdr *bp, Line *l) {
	switch(l->type){
	case Empty:
		//fprint(2, "writeLine: added new line for Empty\n");
		Bputc(bp, '\n');
		break;
	case TableLine:
		if(!font)
			_writeLine(bp, l);
		break;
	case SectionTitle:
	case Text:
		_writeLine(bp, l);
		break;
	}
}
*/

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
		break;
	default:
		usage(nil);
	}ARGEND;
}

#define INDENTATIONCOLLAPSED(l,p) (p && l->type != Empty && \
		l->type != p->type && \
		l->initialspaces - l->margin == p->initialspaces - p->margin)

#define TABLECONTINUES(l,p) (p && p->type == TableLine && \
		l->type != Empty && \
		l->initialspaces >= prev->initialspaces)

void
handleLineOnFixedWidthFont(Biobufhdr *bp, Line *l){
	writeLine(bp, l);
	if(prev){
		freeLine(prev);
		prev = nil;
	}
	if(l->type == Empty)
		freeLine(l);
	else
		prev = l;	
}

void
handleLineOnVariableWidthFont(Biobufhdr *bp, Line *l){
	if(l->type == TableLine){
		updateTable(l);
	}else{
		if(table){
			writeTable();
			freeTable();
			prev = nil;	/* prev has already been freed */
			table = nil;
		}
		writeLine(bp, l);
	}
	if(prev){
		if(l->type != TableLine)
			freeLine(prev);
		prev = nil;
	}
	if(l->type == Empty)
		freeLine(l);
	else
		prev = l;	
}

void
main(int argc, char **argv)
{
	Biobuf bin, bout;
	Line *l;
	LineHandler handleLine;

	parseArguments(argc, argv);
	//fprint(2, "tabstop = %d\n\n", tabstop);

	/*	to keep code simple, use different logic
		with or without font */

	if(font)
		handleLine = handleLineOnVariableWidthFont;
	else
		handleLine = handleLineOnFixedWidthFont;

	margins = (int*)calloc(levels, sizeof(int));

	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);

	while(l = readLine(&bin)) {
		if(TABLECONTINUES(l, prev))
			l->type = TableLine;
		setMargin(l);
		if(addnl && INDENTATIONCOLLAPSED(l, prev)) {
			Bputc(&bout, '\n');
		}
		handleLine(&bout, l);
	}
	if(font && table){
		/* a table is still waiting to be printed */
		writeTable();
	}
	exits(nil);
}